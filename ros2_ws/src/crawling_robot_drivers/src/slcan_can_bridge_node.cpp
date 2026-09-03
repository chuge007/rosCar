#include <array>
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
    if (tx_queue_.size() >= 100) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 1000, "CAN transmit queue is full.");
      return;
    }
    tx_queue_.push_back(frame);
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
      sendOneFrame();
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
      serial_.writeAll(toBytes("O\r"));
      receive_buffer_.clear();
      RCLCPP_INFO(get_logger(), "Opened SLCAN adapter on %s at %d bit/s CAN.",
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
      can_publisher_->publish(message);
    } catch (const std::exception&) {
      RCLCPP_WARN(get_logger(), "Discarded unparsable SLCAN frame.");
    }
  }

  void sendOneFrame() {
    crawling_robot_interfaces::msg::CanFrame frame;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (tx_queue_.empty()) {
        return;
      }
      frame = tx_queue_.front();
      tx_queue_.pop_front();
    }

    std::ostringstream command;
    command << 't' << std::uppercase << std::hex << std::setfill('0') << std::setw(3) << frame.id
            << static_cast<int>(frame.dlc);
    for (std::size_t index = 0; index < frame.dlc; ++index) {
      command << std::setw(2) << static_cast<int>(frame.data[index]);
    }
    command << '\r';
    serial_.writeAll(toBytes(command.str()));
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
