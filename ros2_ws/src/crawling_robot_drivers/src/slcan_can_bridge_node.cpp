#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "crawling_robot_interfaces/msg/can_frame.hpp"
#include "crawling_robot_drivers/win32_serial_port.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {
namespace {

std::string bitrateCommand(int bitrate) {
  switch (bitrate) {
    case 10000:
      return "S0\r";
    case 20000:
      return "S1\r";
    case 50000:
      return "S2\r";
    case 100000:
      return "S3\r";
    case 125000:
      return "S4\r";
    case 250000:
      return "S5\r";
    case 500000:
      return "S6\r";
    case 800000:
      return "S7\r";
    case 1000000:
      return "S8\r";
    default:
      return {};
  }
}

std::vector<std::uint8_t> toBytes(const std::string& value) {
  return {value.begin(), value.end()};
}

int parseHex(const std::string& value) {
  return std::stoi(value, nullptr, 16);
}

}  // namespace

class SlcanCanBridgeNode final : public rclcpp::Node {
public:
  SlcanCanBridgeNode() : Node("slcan_can_bridge_node") {
    serial_port_name_ = declare_parameter<std::string>("serial_port", "");
    serial_baud_rate_ = declare_parameter<int>("serial_baud_rate", 115200);
    can_bitrate_ = declare_parameter<int>("can_bitrate", 500000);
    auto_open_ = declare_parameter<bool>("auto_open", false);
    can_tx_topic_ = declare_parameter<std::string>("can_tx_topic", "/can/tx");
    can_rx_topic_ = declare_parameter<std::string>("can_rx_topic", "/can/rx");

    can_publisher_ = create_publisher<crawling_robot_interfaces::msg::CanFrame>(can_rx_topic_, 100);
    can_subscription_ = create_subscription<crawling_robot_interfaces::msg::CanFrame>(
        can_tx_topic_, 100,
        [this](crawling_robot_interfaces::msg::CanFrame::SharedPtr message) { queueFrame(*message); });
    timer_ = create_wall_timer(std::chrono::milliseconds(2), [this] { poll(); });
  }

private:
  bool hasValidConfiguration() const {
    return auto_open_ && !serial_port_name_.empty() && serial_baud_rate_ > 0 &&
           !bitrateCommand(can_bitrate_).empty();
  }

  void queueFrame(const crawling_robot_interfaces::msg::CanFrame& frame) {
    if (frame.id > 0x7FFU || frame.dlc > 8) {
      RCLCPP_ERROR(get_logger(), "SLCAN only accepts standard CAN frames with DLC up to 8.");
      return;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    const bool is_drive_speed = frame.id >= 0x141U && frame.id <= 0x160U && frame.dlc >= 1 &&
                                frame.data[0] == 0xA2U;
    const bool is_drive_stop = frame.id >= 0x141U && frame.id <= 0x160U && frame.dlc >= 1 &&
                               frame.data[0] == 0x81U;
    if (is_drive_speed || is_drive_stop) {
      // A speed frame supersedes every older speed request for the same motor.
      // Keeping that backlog was the source of delayed, jerky movement when a
      // USB-CAN adapter temporarily fell behind.
      for (auto iterator = tx_queue_.begin(); iterator != tx_queue_.end();) {
        if (iterator->id == frame.id && iterator->dlc >= 1 &&
            (iterator->data[0] == 0xA2U || iterator->data[0] == 0x81U)) {
          iterator = tx_queue_.erase(iterator);
          ++coalesced_drive_frames_;
        } else {
          ++iterator;
        }
      }
      if (is_drive_stop) {
        tx_queue_.push_front(frame);
        ++priority_stop_frames_;
      } else {
        tx_queue_.push_back(frame);
      }
      ++queued_frames_;
      return;
    }
    constexpr std::size_t kMaxTransmitQueue = 100;
    if (tx_queue_.size() >= kMaxTransmitQueue) {
      // Motion commands are periodic. Keeping an old backlog is worse than
      // dropping the oldest frame because it makes a newly pressed button
      // take effect only after stale commands have been transmitted.
      tx_queue_.pop_front();
      ++dropped_frames_;
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 1000,
                           "CAN transmit queue saturated; dropping oldest frame.");
    }
    tx_queue_.push_back(frame);
    ++queued_frames_;
  }

  void poll() {
    if (!hasValidConfiguration()) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 10000,
          "SLCAN bridge is inactive. Enable auto_open and configure serial_port and CAN bitrate.");
      return;
    }

    connectIfNeeded();
    if (!serial_.isOpen()) {
      return;
    }

    try {
      readFrames();
      // Keep normal traffic moving, but never let a burst accumulate enough
      // stale frames to delay the next manual speed command.
      sendQueuedFrames();
      logTransmitStatistics();
    } catch (const std::exception& error) {
      RCLCPP_ERROR(get_logger(), "SLCAN serial I/O failed: %s", error.what());
      serial_.close();
    }
  }

  void connectIfNeeded() {
    if (serial_.isOpen()) {
      return;
    }
    const auto now_steady = std::chrono::steady_clock::now();
    if (now_steady - last_connect_attempt_ < std::chrono::seconds(1)) {
      return;
    }
    last_connect_attempt_ = now_steady;

    try {
      serial_.open(serial_port_name_, static_cast<std::uint32_t>(serial_baud_rate_));
      serial_.writeAll(toBytes("C\r"));
      serial_.writeAll(toBytes(bitrateCommand(can_bitrate_)));
      // Motion frames are refreshed every 20 ms. Do not let the adapter keep
      // retrying an old command and block newer speed or stop frames.
      serial_.writeAll(toBytes("M0\r"));
      serial_.writeAll(toBytes("A0\r"));
      serial_.writeAll(toBytes("O\r"));
      receive_buffer_.clear();
      RCLCPP_INFO(get_logger(),
                  "Opened SLCAN adapter on %s at %d bit/s CAN with stale-frame retries disabled.",
                  serial_port_name_.c_str(), can_bitrate_);
    } catch (const std::exception& error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "SLCAN connection failed: %s", error.what());
      serial_.close();
    }
  }

  void readFrames() {
    std::array<std::uint8_t, 512> bytes{};
    const std::size_t received = serial_.read(bytes.data(), bytes.size());
    receive_buffer_.append(reinterpret_cast<const char*>(bytes.data()), received);

    while (true) {
      const std::size_t end = receive_buffer_.find('\r');
      if (end == std::string::npos) {
        break;
      }
      const std::string frame = receive_buffer_.substr(0, end);
      receive_buffer_.erase(0, end + 1);
      publishFrame(frame);
    }
  }

  void publishFrame(const std::string& frame) {
    if (frame.empty() || frame.front() != 't' || frame.size() < 5) {
      return;
    }

    try {
      const std::uint32_t id = static_cast<std::uint32_t>(parseHex(frame.substr(1, 3)));
      const int dlc = parseHex(frame.substr(4, 1));
      if (dlc < 0 || dlc > 8 || frame.size() != static_cast<std::size_t>(5 + dlc * 2)) {
        RCLCPP_WARN(get_logger(), "Discarded malformed SLCAN frame.");
        return;
      }
      crawling_robot_interfaces::msg::CanFrame message;
      message.header.stamp = now();
      message.id = id;
      message.dlc = static_cast<std::uint8_t>(dlc);
      for (int index = 0; index < dlc; ++index) {
        message.data[static_cast<std::size_t>(index)] =
            static_cast<std::uint8_t>(parseHex(frame.substr(5 + index * 2, 2)));
      }
      ++received_frames_;
      if (id > 0x240U && id <= 0x260U && dlc >= 1 &&
          message.data[0] == 0xA2U) {
        ++speed_feedback_by_motor_[id - 0x240U];
      }
      can_publisher_->publish(message);
    } catch (const std::exception&) {
      ++unparsable_frames_;
      RCLCPP_WARN(get_logger(), "Discarded unparsable SLCAN frame.");
    }
  }

  void sendQueuedFrames() {
    constexpr std::size_t kMaxFramesPerSerialWrite = 4;
    std::vector<crawling_robot_interfaces::msg::CanFrame> frames;
    frames.reserve(kMaxFramesPerSerialWrite);
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      while (!tx_queue_.empty() && frames.size() < kMaxFramesPerSerialWrite) {
        frames.push_back(tx_queue_.front());
        tx_queue_.pop_front();
      }
    }
    if (frames.empty()) return;

    std::ostringstream command;
    std::size_t drive_speed_frames = 0;
    std::ostringstream drive_ids;
    double max_queue_age_ms = 0.0;
    for (const auto& frame : frames) {
      command << 't' << std::uppercase << std::hex << std::setfill('0') << std::setw(3) << frame.id
              << static_cast<int>(frame.dlc);
      for (std::size_t index = 0; index < frame.dlc; ++index) {
        command << std::setw(2) << static_cast<int>(frame.data[index]);
      }
      command << '\r';
      const bool is_drive_speed = frame.id >= 0x141U && frame.id <= 0x160U &&
                                  frame.dlc >= 1 && frame.data[0] == 0xA2U;
      if (is_drive_speed) {
        ++drive_speed_frames;
        if (drive_ids.tellp() > 0) drive_ids << ',';
        drive_ids << "0x" << std::uppercase << std::hex << frame.id;
        const auto queued_at = rclcpp::Time(frame.header.stamp);
        max_queue_age_ms = std::max(max_queue_age_ms, (now() - queued_at).seconds() * 1000.0);
      }
    }
    const auto serial_start = std::chrono::steady_clock::now();
    serial_.writeAll(toBytes(command.str()));
    const auto serial_end = std::chrono::steady_clock::now();
    sent_frames_ += frames.size();
    if (drive_speed_frames > 0) {
      ++drive_tx_batches_;
      drive_tx_frames_ += drive_speed_frames;
      drive_tx_max_queue_age_ms_ = std::max(drive_tx_max_queue_age_ms_, max_queue_age_ms);
      drive_tx_max_submit_us_ = std::max(
          drive_tx_max_submit_us_,
          std::chrono::duration_cast<std::chrono::microseconds>(serial_end - serial_start).count());
      last_drive_tx_ids_ = drive_ids.str();
      last_drive_tx_frame_count_ = drive_speed_frames;
    }
  }

  void logTransmitStatistics() {
    std::size_t queue_depth = 0;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      queue_depth = tx_queue_.size();
    }
    std::ostringstream feedback_counts;
    for (std::size_t motor_id = 1; motor_id < speed_feedback_by_motor_.size(); ++motor_id) {
      if (speed_feedback_by_motor_[motor_id] == 0) continue;
      if (feedback_counts.tellp() > 0) feedback_counts << ',';
      feedback_counts << "id" << motor_id << '=' << speed_feedback_by_motor_[motor_id];
    }
    RCLCPP_INFO_THROTTLE(
        get_logger(), *get_clock(), 1000,
        "SLCAN TX: queued=%llu sent=%llu coalesced_drive=%llu dropped=%llu "
        "priority_stop=%llu queue_depth=%zu; drive_batches=%llu drive_frames=%llu "
        "last_drive=[count=%zu ids=%s] max_queue_age=%.2f ms max_serial_submit=%lld us; "
        "rx_frames=%llu feedback=%s unparsable=%llu.",
        static_cast<unsigned long long>(queued_frames_), static_cast<unsigned long long>(sent_frames_),
        static_cast<unsigned long long>(coalesced_drive_frames_),
        static_cast<unsigned long long>(dropped_frames_),
        static_cast<unsigned long long>(priority_stop_frames_), queue_depth,
        static_cast<unsigned long long>(drive_tx_batches_),
        static_cast<unsigned long long>(drive_tx_frames_), last_drive_tx_frame_count_,
        last_drive_tx_ids_.c_str(), drive_tx_max_queue_age_ms_,
        static_cast<long long>(drive_tx_max_submit_us_),
        static_cast<unsigned long long>(received_frames_), feedback_counts.str().c_str(),
        static_cast<unsigned long long>(unparsable_frames_));
    drive_tx_batches_ = 0;
    drive_tx_frames_ = 0;
    drive_tx_max_queue_age_ms_ = 0.0;
    drive_tx_max_submit_us_ = 0;
    received_frames_ = 0;
    unparsable_frames_ = 0;
    speed_feedback_by_motor_.fill(0);
  }

  std::string serial_port_name_;
  int serial_baud_rate_ = 115200;
  int can_bitrate_ = 1000000;
  bool auto_open_ = false;
  std::string can_tx_topic_;
  std::string can_rx_topic_;
  Win32SerialPort serial_;
  std::string receive_buffer_;
  std::chrono::steady_clock::time_point last_connect_attempt_{};
  std::mutex queue_mutex_;
  std::deque<crawling_robot_interfaces::msg::CanFrame> tx_queue_;
  std::uint64_t queued_frames_ = 0;
  std::uint64_t sent_frames_ = 0;
  std::uint64_t coalesced_drive_frames_ = 0;
  std::uint64_t dropped_frames_ = 0;
  std::uint64_t priority_stop_frames_ = 0;
  std::uint64_t drive_tx_batches_ = 0;
  std::uint64_t drive_tx_frames_ = 0;
  std::size_t last_drive_tx_frame_count_ = 0;
  std::string last_drive_tx_ids_;
  double drive_tx_max_queue_age_ms_ = 0.0;
  std::int64_t drive_tx_max_submit_us_ = 0;
  std::uint64_t received_frames_ = 0;
  std::uint64_t unparsable_frames_ = 0;
  std::array<std::uint64_t, 33> speed_feedback_by_motor_{};
  rclcpp::Publisher<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_publisher_;
  rclcpp::Subscription<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace crawling_robot_drivers

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("slcan_can_bridge_node");
  rclcpp::spin(std::make_shared<crawling_robot_drivers::SlcanCanBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
