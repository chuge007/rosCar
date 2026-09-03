#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"

#include "crawling_robot_drivers/rim302_protocol.hpp"
#include "crawling_robot_drivers/win32_serial_port.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {
namespace {

geometry_msgs::msg::Quaternion toQuaternion(double roll, double pitch, double yaw) {
  const double cy = std::cos(yaw * 0.5);
  const double sy = std::sin(yaw * 0.5);
  const double cp = std::cos(pitch * 0.5);
  const double sp = std::sin(pitch * 0.5);
  const double cr = std::cos(roll * 0.5);
  const double sr = std::sin(roll * 0.5);

  geometry_msgs::msg::Quaternion result;
  result.w = cr * cp * cy + sr * sp * sy;
  result.x = sr * cp * cy - cr * sp * sy;
  result.y = cr * sp * cy + sr * cp * sy;
  result.z = cr * cp * sy - sr * sp * cy;
  return result;
}

}  // namespace

class Rim302ImuNode final : public rclcpp::Node {
public:
  Rim302ImuNode() : Node("rim302_imu_node") {
    serial_port_name_ = declare_parameter<std::string>("serial_port", "");
    baud_rate_ = declare_parameter<int>("baud_rate", 115200);
    frame_id_ = declare_parameter<std::string>("frame_id", "imu_link");
    configure_output_ = declare_parameter<bool>("configure_output", false);
    output_divider_ = declare_parameter<int>("output_divider", 1);
    query_default_output_ = declare_parameter<bool>("query_default_output", false);
    query_period_ms_ = declare_parameter<int>("query_period_ms", 100);
    convert_frd_to_flu_ = declare_parameter<bool>("convert_frd_to_flu", true);

    publisher_ = create_publisher<sensor_msgs::msg::Imu>("/imu/data", rclcpp::SensorDataQoS());
    timer_ = create_wall_timer(std::chrono::milliseconds(5), [this] { poll(); });
  }

private:
  void connectIfNeeded() {
    if (serial_.isOpen() || serial_port_name_.empty() || baud_rate_ <= 0) {
      return;
    }
    if (configure_output_ && (output_divider_ < 1 || output_divider_ > 200)) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000,
                            "RIM302 output_divider must be between 1 and 200.");
      return;
    }
    if (query_default_output_ && query_period_ms_ < 1) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000,
                            "IMU query_period_ms must be positive.");
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_connect_attempt_ < std::chrono::seconds(1)) {
      return;
    }
    last_connect_attempt_ = now;

    try {
      serial_.open(serial_port_name_, static_cast<std::uint32_t>(baud_rate_));
      RCLCPP_INFO(get_logger(), "Connected to IMU on %s at %d bps.", serial_port_name_.c_str(),
                  baud_rate_);
      if (configure_output_) {
        serial_.writeAll(
            Rim302Protocol::buildSetContinuousMode(static_cast<std::uint8_t>(output_divider_)));
        RCLCPP_INFO(get_logger(), "Enabled IMU continuous output with divider %d.",
                    output_divider_);
      }
    } catch (const std::exception& error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "RIM302 connection failed: %s",
                           error.what());
      serial_.close();
    }
  }

  void poll() {
    if (serial_port_name_.empty() || baud_rate_ <= 0) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
                           "RIM302 is inactive. Set serial_port and baud_rate first.");
      return;
    }
    connectIfNeeded();
    if (!serial_.isOpen()) {
      return;
    }

    try {
      const auto now = std::chrono::steady_clock::now();
      if (query_default_output_ &&
          (last_query_time_ == std::chrono::steady_clock::time_point{} ||
           now - last_query_time_ >= std::chrono::milliseconds(query_period_ms_))) {
        serial_.writeAll(Rim302Protocol::buildGetDefaultOutput());
        last_query_time_ = now;
      }
      std::array<std::uint8_t, 512> bytes{};
      const std::size_t received = serial_.read(bytes.data(), bytes.size());
      for (const auto& sample : parser_.consume(bytes.data(), received)) {
        publish(sample);
      }
    } catch (const std::exception& error) {
      RCLCPP_ERROR(get_logger(), "RIM302 serial read failed: %s", error.what());
      serial_.close();
    }
  }

  void publish(const Rim302Sample& sample) {
    sensor_msgs::msg::Imu message;
    message.header.stamp = now();
    message.header.frame_id = frame_id_;
    message.orientation = toQuaternion(sample.roll_rad, sample.pitch_rad, sample.yaw_rad);
    message.angular_velocity.x = sample.angular_velocity_x_rad_s;
    message.angular_velocity.y = sample.angular_velocity_y_rad_s;
    message.angular_velocity.z = sample.angular_velocity_z_rad_s;
    message.linear_acceleration.x = sample.linear_acceleration_x_m_s2;
    message.linear_acceleration.y = sample.linear_acceleration_y_m_s2;
    message.linear_acceleration.z = sample.linear_acceleration_z_m_s2;
    if (convert_frd_to_flu_) {
      message.orientation.y = -message.orientation.y;
      message.orientation.z = -message.orientation.z;
      message.angular_velocity.y = -message.angular_velocity.y;
      message.angular_velocity.z = -message.angular_velocity.z;
      message.linear_acceleration.y = -message.linear_acceleration.y;
      message.linear_acceleration.z = -message.linear_acceleration.z;
    }
    message.orientation_covariance[0] = 0.01;
    message.orientation_covariance[4] = 0.01;
    message.orientation_covariance[8] = 0.04;
    message.angular_velocity_covariance[0] = 0.001;
    message.angular_velocity_covariance[4] = 0.001;
    message.angular_velocity_covariance[8] = 0.001;
    message.linear_acceleration_covariance[0] = 0.05;
    message.linear_acceleration_covariance[4] = 0.05;
    message.linear_acceleration_covariance[8] = 0.05;
    publisher_->publish(message);
  }

  std::string serial_port_name_;
  int baud_rate_ = 115200;
  std::string frame_id_;
  bool configure_output_ = false;
  int output_divider_ = 1;
  bool query_default_output_ = false;
  int query_period_ms_ = 100;
  bool convert_frd_to_flu_ = true;
  Win32SerialPort serial_;
  Rim302FrameParser parser_;
  std::chrono::steady_clock::time_point last_connect_attempt_{};
  std::chrono::steady_clock::time_point last_query_time_{};
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace crawling_robot_drivers

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("rim302_imu_node");
  rclcpp::spin(std::make_shared<crawling_robot_drivers::Rim302ImuNode>());
  rclcpp::shutdown();
  return 0;
}
