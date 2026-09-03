#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int64.hpp"

#include "crawling_robot_drivers/win32_serial_port.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {
namespace {

std::uint16_t modbusCrc16(const std::uint8_t* bytes, std::size_t size) {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= bytes[index];
    for (int bit = 0; bit < 8; ++bit) {
      const bool low_bit_set = (crc & 0x0001U) != 0U;
      crc >>= 1U;
      if (low_bit_set) {
        crc ^= 0xA001U;
      }
    }
  }
  return crc;
}

std::vector<std::uint8_t> readHoldingRegistersRequest(std::uint8_t unit_id,
                                                       std::uint16_t register_address) {
  std::vector<std::uint8_t> request = {
      unit_id,
      0x03,
      static_cast<std::uint8_t>(register_address >> 8U),
      static_cast<std::uint8_t>(register_address & 0xFFU),
      0x00,
      0x02,
  };
  const std::uint16_t crc = modbusCrc16(request.data(), request.size());
  request.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
  request.push_back(static_cast<std::uint8_t>(crc >> 8U));
  return request;
}

}  // namespace

class ModbusEncoderNode final : public rclcpp::Node {
public:
  ModbusEncoderNode() : Node("modbus_encoder_node") {
    serial_port_name_ = declare_parameter<std::string>("serial_port", "");
    baud_rate_ = declare_parameter<int>("baud_rate", 115200);
    unit_id_ = declare_parameter<int>("unit_id", 1);
    counter_register_ = declare_parameter<int>("counter_register", -1);
    high_word_first_ = declare_parameter<bool>("high_word_first", true);
    read_interval_ms_ = declare_parameter<int>("read_interval_ms", 20);
    response_timeout_ms_ = declare_parameter<int>("response_timeout_ms", 100);
    ticks_topic_ = declare_parameter<std::string>("ticks_topic", "/scan_encoder/ticks");

    publisher_ = create_publisher<std_msgs::msg::Int64>(ticks_topic_, rclcpp::SensorDataQoS());
    timer_ = create_wall_timer(std::chrono::milliseconds(2), [this] { poll(); });
  }

private:
  bool hasValidConfiguration() const {
    return !serial_port_name_.empty() && baud_rate_ > 0 && unit_id_ >= 1 && unit_id_ <= 247 &&
           counter_register_ >= 0 && counter_register_ <= 65535 && read_interval_ms_ > 0 &&
           response_timeout_ms_ > 0;
  }

  void connectIfNeeded() {
    if (serial_.isOpen() || !hasValidConfiguration()) {
      return;
    }

    const auto now_steady = std::chrono::steady_clock::now();
    if (now_steady - last_connect_attempt_ < std::chrono::seconds(1)) {
      return;
    }
    last_connect_attempt_ = now_steady;

    try {
      serial_.open(serial_port_name_, static_cast<std::uint32_t>(baud_rate_));
      response_buffer_.clear();
      request_pending_ = false;
      RCLCPP_INFO(get_logger(), "Connected to Modbus encoder on %s at %d bps.",
                  serial_port_name_.c_str(), baud_rate_);
    } catch (const std::exception& error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Modbus encoder connection failed: %s", error.what());
      serial_.close();
    }
  }

  void poll() {
    if (!hasValidConfiguration()) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 10000,
          "Modbus encoder is inactive. Set serial_port, unit_id, and counter_register first.");
      return;
    }

    connectIfNeeded();
    if (!serial_.isOpen()) {
      return;
    }

    try {
      receiveResponses();
      const auto now_steady = std::chrono::steady_clock::now();
      if (request_pending_ && now_steady - request_sent_at_ >
                                  std::chrono::milliseconds(response_timeout_ms_)) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                             "Timed out waiting for Modbus encoder response.");
        request_pending_ = false;
      }
      if (!request_pending_ && now_steady - last_request_at_ >=
                                   std::chrono::milliseconds(read_interval_ms_)) {
        serial_.writeAll(readHoldingRegistersRequest(
            static_cast<std::uint8_t>(unit_id_), static_cast<std::uint16_t>(counter_register_)));
        request_pending_ = true;
        request_sent_at_ = now_steady;
        last_request_at_ = now_steady;
      }
    } catch (const std::exception& error) {
      RCLCPP_ERROR(get_logger(), "Modbus encoder serial I/O failed: %s", error.what());
      serial_.close();
      request_pending_ = false;
    }
  }

  void receiveResponses() {
    std::array<std::uint8_t, 256> bytes{};
    const std::size_t received = serial_.read(bytes.data(), bytes.size());
    response_buffer_.insert(response_buffer_.end(), bytes.begin(), bytes.begin() + received);

    while (response_buffer_.size() >= 5) {
      const auto expected_unit = static_cast<std::uint8_t>(unit_id_);
      if (response_buffer_[0] != expected_unit) {
        response_buffer_.erase(response_buffer_.begin());
        continue;
      }

      const std::uint8_t function = response_buffer_[1];
      if ((function & 0x80U) != 0U) {
        if (response_buffer_.size() < 5) {
          return;
        }
        RCLCPP_ERROR(get_logger(), "Modbus encoder returned exception code 0x%02X.",
                     response_buffer_[2]);
        response_buffer_.erase(response_buffer_.begin(), response_buffer_.begin() + 5);
        request_pending_ = false;
        continue;
      }
      if (function != 0x03U) {
        response_buffer_.erase(response_buffer_.begin());
        continue;
      }

      const std::size_t byte_count = response_buffer_[2];
      const std::size_t frame_size = byte_count + 5;
      if (response_buffer_.size() < frame_size) {
        return;
      }
      const std::uint16_t expected_crc = static_cast<std::uint16_t>(response_buffer_[frame_size - 2]) |
                                         static_cast<std::uint16_t>(response_buffer_[frame_size - 1] << 8U);
      const std::uint16_t actual_crc = modbusCrc16(response_buffer_.data(), frame_size - 2);
      if (actual_crc != expected_crc) {
        response_buffer_.erase(response_buffer_.begin());
        continue;
      }
      if (byte_count != 4) {
        RCLCPP_ERROR(get_logger(), "Expected a 32-bit encoder count but received %zu data bytes.",
                     byte_count);
        response_buffer_.erase(response_buffer_.begin(),
                               response_buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        request_pending_ = false;
        continue;
      }

      const std::uint16_t first_word =
          static_cast<std::uint16_t>(response_buffer_[3] << 8U) | response_buffer_[4];
      const std::uint16_t second_word =
          static_cast<std::uint16_t>(response_buffer_[5] << 8U) | response_buffer_[6];
      const std::uint32_t raw_ticks = high_word_first_
                                          ? (static_cast<std::uint32_t>(first_word) << 16U) | second_word
                                          : (static_cast<std::uint32_t>(second_word) << 16U) | first_word;
      std_msgs::msg::Int64 message;
      message.data = static_cast<std::int32_t>(raw_ticks);
      publisher_->publish(message);
      response_buffer_.erase(response_buffer_.begin(),
                             response_buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
      request_pending_ = false;
    }
  }

  std::string serial_port_name_;
  int baud_rate_ = 115200;
  int unit_id_ = 1;
  int counter_register_ = -1;
  bool high_word_first_ = true;
  int read_interval_ms_ = 20;
  int response_timeout_ms_ = 100;
  std::string ticks_topic_;
  Win32SerialPort serial_;
  std::vector<std::uint8_t> response_buffer_;
  bool request_pending_ = false;
  std::chrono::steady_clock::time_point request_sent_at_{};
  std::chrono::steady_clock::time_point last_request_at_{};
  std::chrono::steady_clock::time_point last_connect_attempt_{};
  rclcpp::Publisher<std_msgs::msg::Int64>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace crawling_robot_drivers

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("modbus_encoder_node");
  rclcpp::spin(std::make_shared<crawling_robot_drivers::ModbusEncoderNode>());
  rclcpp::shutdown();
  return 0;
}
