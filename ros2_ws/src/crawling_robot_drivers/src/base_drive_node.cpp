#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_srvs/srv/set_bool.hpp"

#include "crawling_robot_interfaces/msg/can_frame.hpp"
#include "crawling_robot_drivers/servo_v38.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {
namespace {

constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

crawling_robot_interfaces::msg::CanFrame toRosFrame(const CanFrame& frame,
                                                     const rclcpp::Time& stamp) {
  crawling_robot_interfaces::msg::CanFrame message;
  message.header.stamp = stamp;
  message.id = frame.id;
  message.dlc = 8;
  message.data = frame.data;
  return message;
}

CanFrame fromRosFrame(const crawling_robot_interfaces::msg::CanFrame& message) {
  CanFrame frame;
  frame.id = message.id;
  frame.data = message.data;
  return frame;
}

}  // namespace

class BaseDriveNode final : public rclcpp::Node {
public:
  BaseDriveNode() : Node("base_drive_node") {
    left_motor_id_ = static_cast<std::uint8_t>(declare_parameter<int>("left_motor_id", 1));
    right_motor_id_ = static_cast<std::uint8_t>(declare_parameter<int>("right_motor_id", 2));
    left_motor_sign_ = declare_parameter<int>("left_motor_sign", 1);
    right_motor_sign_ = declare_parameter<int>("right_motor_sign", 1);
    wheel_radius_m_ = declare_parameter<double>("wheel_radius_m", 0.0);
    wheel_separation_m_ = declare_parameter<double>("wheel_separation_m", 0.0);
    motor_output_to_wheel_ratio_ = declare_parameter<double>("motor_output_to_wheel_ratio", 1.0);
    max_wheel_speed_m_s_ = declare_parameter<double>("max_wheel_speed_m_s", 0.0);
    command_timeout_ms_ = declare_parameter<int>("command_timeout_ms", 200);
    can_tx_topic_ = declare_parameter<std::string>("can_tx_topic", "/can/tx");
    can_rx_topic_ = declare_parameter<std::string>("can_rx_topic", "/can/rx");

    can_publisher_ = create_publisher<crawling_robot_interfaces::msg::CanFrame>(can_tx_topic_, 50);
    joint_state_publisher_ = create_publisher<sensor_msgs::msg::JointState>("/drive/joint_states", 20);
    command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
        "/cmd_vel", 20, [this](geometry_msgs::msg::Twist::SharedPtr message) { onCommand(*message); });
    can_subscription_ = create_subscription<crawling_robot_interfaces::msg::CanFrame>(
        can_rx_topic_, 100,
        [this](crawling_robot_interfaces::msg::CanFrame::SharedPtr message) { onCanFrame(*message); });
    enable_service_ = create_service<std_srvs::srv::SetBool>(
        "/drive/enable", [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
                                  std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
          onEnable(*request, *response);
        });
    timer_ = create_wall_timer(std::chrono::milliseconds(20), [this] { updateDrive(); });
  }

private:
  void onCommand(const geometry_msgs::msg::Twist& command) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    command_ = command;
    last_command_time_ = now();
    has_command_ = true;
  }

  void onEnable(const std_srvs::srv::SetBool::Request& request,
                std_srvs::srv::SetBool::Response& response) {
    if (request.data && !hasValidGeometry()) {
      response.success = false;
      response.message = "Set valid wheel geometry, limits, motor IDs, and motor signs first.";
      return;
    }

    drive_enabled_ = request.data;
    if (!drive_enabled_) {
      sendStops();
      stopped_ = true;
    }
    response.success = true;
    response.message = drive_enabled_ ? "Drive command output enabled." : "Drive command output disabled.";
  }

  void updateDrive() {
    geometry_msgs::msg::Twist command;
    bool command_is_fresh = false;
    {
      std::lock_guard<std::mutex> lock(command_mutex_);
      command = command_;
      command_is_fresh = has_command_ &&
                         (now() - last_command_time_).nanoseconds() <=
                             static_cast<std::int64_t>(command_timeout_ms_) * 1000000LL;
    }

    if (!drive_enabled_ || !command_is_fresh || !hasValidGeometry()) {
      if (!stopped_) {
        sendStops();
        stopped_ = true;
      }
      return;
    }

    const double left_speed_m_s = command.linear.x - command.angular.z * wheel_separation_m_ * 0.5;
    const double right_speed_m_s = command.linear.x + command.angular.z * wheel_separation_m_ * 0.5;
    sendSpeed(left_motor_id_, left_speed_m_s, left_motor_sign_);
    sendSpeed(right_motor_id_, right_speed_m_s, right_motor_sign_);
    stopped_ = false;
  }

  bool hasValidGeometry() const {
    return wheel_radius_m_ > 0.0 && wheel_separation_m_ > 0.0 &&
           motor_output_to_wheel_ratio_ > 0.0 && max_wheel_speed_m_s_ > 0.0 &&
           left_motor_id_ >= 1 && left_motor_id_ <= 32 && right_motor_id_ >= 1 &&
           right_motor_id_ <= 32 && (left_motor_sign_ == -1 || left_motor_sign_ == 1) &&
           (right_motor_sign_ == -1 || right_motor_sign_ == 1);
  }

  void sendSpeed(std::uint8_t motor_id, double wheel_speed_m_s, int motor_sign) {
    const double bounded_speed = std::clamp(wheel_speed_m_s, -max_wheel_speed_m_s_, max_wheel_speed_m_s_);
    const double motor_speed_dps = bounded_speed / wheel_radius_m_ * kRadiansToDegrees *
                                   motor_output_to_wheel_ratio_ * static_cast<double>(motor_sign);
    sendFrame(ServoV38::speedCommand(motor_id, motor_speed_dps));
  }

  void sendStops() {
    sendFrame(ServoV38::stopCommand(left_motor_id_));
    sendFrame(ServoV38::stopCommand(right_motor_id_));
  }

  void sendFrame(const CanFrame& frame) {
    can_publisher_->publish(toRosFrame(frame, now()));
  }

  void onCanFrame(const crawling_robot_interfaces::msg::CanFrame& frame) {
    if (frame.dlc < 8) {
      return;
    }
    const auto feedback = ServoV38::parseFeedback(fromRosFrame(frame));
    if (!feedback || (feedback->motor_id != left_motor_id_ && feedback->motor_id != right_motor_id_)) {
      return;
    }

    const bool is_left = feedback->motor_id == left_motor_id_;
    const int sign = is_left ? left_motor_sign_ : right_motor_sign_;
    sensor_msgs::msg::JointState state;
    state.header.stamp = now();
    state.name.push_back(is_left ? "left_drive_wheel_joint" : "right_drive_wheel_joint");
    state.position.push_back(feedback->output_angle_deg / motor_output_to_wheel_ratio_ /
                             static_cast<double>(sign) / kRadiansToDegrees);
    state.velocity.push_back(feedback->output_speed_dps / motor_output_to_wheel_ratio_ /
                             static_cast<double>(sign) / kRadiansToDegrees);
    state.effort.push_back(feedback->torque_current_a);
    joint_state_publisher_->publish(state);
  }

  std::uint8_t left_motor_id_ = 1;
  std::uint8_t right_motor_id_ = 2;
  int left_motor_sign_ = 1;
  int right_motor_sign_ = 1;
  double wheel_radius_m_ = 0.0;
  double wheel_separation_m_ = 0.0;
  double motor_output_to_wheel_ratio_ = 1.0;
  double max_wheel_speed_m_s_ = 0.0;
  int command_timeout_ms_ = 200;
  std::string can_tx_topic_;
  std::string can_rx_topic_;
  bool drive_enabled_ = false;
  bool stopped_ = false;
  bool has_command_ = false;
  geometry_msgs::msg::Twist command_;
  rclcpp::Time last_command_time_{0, 0, RCL_ROS_TIME};
  std::mutex command_mutex_;
  rclcpp::Publisher<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joint_state_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_subscription_;
  rclcpp::Subscription<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_subscription_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr enable_service_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace crawling_robot_drivers

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("base_drive_node");
  rclcpp::spin(std::make_shared<crawling_robot_drivers::BaseDriveNode>());
  rclcpp::shutdown();
  return 0;
}
