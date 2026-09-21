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
#include "crawling_robot_interfaces/msg/drive_command_status.hpp"
#include "crawling_robot_interfaces/srv/set_drive_limits.hpp"
#include "crawling_robot_interfaces/srv/set_motor_mapping.hpp"
#include "crawling_robot_drivers/differential_drive.hpp"
#include "crawling_robot_drivers/servo_v38.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {
namespace {

constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
constexpr double kControlPeriodSeconds = 0.02;
constexpr double kMaxLinearSpeedMetersPerSecond = 0.30;
constexpr double kMaxAngularSpeedRadiansPerSecond = 2.0;
constexpr double kMaxLinearAccelMetersPerSecondSquared = 1.0;
constexpr double kMaxAngularAccelRadiansPerSecondSquared = 5.0;
constexpr double kMaxWheelSpeedMetersPerSecond = 1.0;

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
    right_motor_sign_ = declare_parameter<int>("right_motor_sign", -1);
    wheel_radius_m_ = declare_parameter<double>("wheel_radius_m", 0.0);
    wheel_separation_m_ = declare_parameter<double>("wheel_separation_m", 0.0);
    motor_output_to_wheel_ratio_ = declare_parameter<double>("motor_output_to_wheel_ratio", 1.0);
    max_wheel_speed_m_s_ = declare_parameter<double>("max_wheel_speed_m_s", 0.0);
    minimum_inner_wheel_ratio_ = declare_parameter<double>("minimum_inner_wheel_ratio", 0.5);
    max_linear_speed_m_s_ = declare_parameter<double>("max_linear_speed_m_s", 0.0);
    max_angular_speed_rad_s_ = declare_parameter<double>("max_angular_speed_rad_s", 0.0);
    max_linear_accel_m_s2_ = declare_parameter<double>("max_linear_accel_m_s2", 0.0);
    max_angular_accel_rad_s2_ = declare_parameter<double>("max_angular_accel_rad_s2", 0.0);
    command_timeout_ms_ = declare_parameter<int>("command_timeout_ms", 200);
    can_tx_topic_ = declare_parameter<std::string>("can_tx_topic", "/can/tx");
    can_rx_topic_ = declare_parameter<std::string>("can_rx_topic", "/can/rx");

    can_publisher_ = create_publisher<crawling_robot_interfaces::msg::CanFrame>(can_tx_topic_, 50);
    joint_state_publisher_ = create_publisher<sensor_msgs::msg::JointState>("/drive/joint_states", 20);
    command_status_publisher_ = create_publisher<
        crawling_robot_interfaces::msg::DriveCommandStatus>("/drive/command_status", 20);
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
    limits_service_ = create_service<crawling_robot_interfaces::srv::SetDriveLimits>(
        "/drive/set_limits",
        [this](const std::shared_ptr<crawling_robot_interfaces::srv::SetDriveLimits::Request> request,
               std::shared_ptr<crawling_robot_interfaces::srv::SetDriveLimits::Response> response) {
          onSetLimits(*request, *response);
        });
    mapping_service_ = create_service<crawling_robot_interfaces::srv::SetMotorMapping>(
        "/drive/set_motor_mapping",
        [this](const std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping::Request> request,
               std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping::Response> response) {
          onSetMotorMapping(*request, *response);
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
    RCLCPP_INFO(get_logger(), "Drive output %s.", drive_enabled_ ? "enabled" : "disabled");
    response.success = true;
    response.message = drive_enabled_ ? "Drive command output enabled." : "Drive command output disabled.";
  }

  void onSetLimits(const crawling_robot_interfaces::srv::SetDriveLimits::Request& request,
                   crawling_robot_interfaces::srv::SetDriveLimits::Response& response) {
    if (!validLimits(request.max_linear_speed_m_s, request.max_angular_speed_rad_s,
                     request.max_linear_accel_m_s2, request.max_angular_accel_rad_s2) ||
        !std::isfinite(request.max_wheel_speed_m_s) || request.max_wheel_speed_m_s <= 0.0 ||
        request.max_wheel_speed_m_s > kMaxWheelSpeedMetersPerSecond ||
        !std::isfinite(request.minimum_inner_wheel_ratio) ||
        request.minimum_inner_wheel_ratio < 0.0 || request.minimum_inner_wheel_ratio > 1.0) {
      response.success = false;
      response.message = "Drive limits exceed the supported range (linear <= 0.30 m/s, angular <= 2.0 rad/s, wheel <= 1.0 m/s).";
      return;
    }
    max_linear_speed_m_s_ = request.max_linear_speed_m_s;
    max_angular_speed_rad_s_ = request.max_angular_speed_rad_s;
    max_linear_accel_m_s2_ = request.max_linear_accel_m_s2;
    max_angular_accel_rad_s2_ = request.max_angular_accel_rad_s2;
    max_wheel_speed_m_s_ = request.max_wheel_speed_m_s;
    minimum_inner_wheel_ratio_ = request.minimum_inner_wheel_ratio;
    // Keep the current ramp state when limits are re-applied. The monitor may
    // restore settings more than once while services come online; resetting
    // here would restart acceleration on every duplicate request and make the
    // commanded speed appear stuck at the first ramp step.
    applied_linear_m_s_ = std::clamp(applied_linear_m_s_, -max_linear_speed_m_s_, max_linear_speed_m_s_);
    applied_angular_rad_s_ = std::clamp(applied_angular_rad_s_, -max_angular_speed_rad_s_, max_angular_speed_rad_s_);
    RCLCPP_INFO(get_logger(),
                "Drive limits applied: linear=%.3f m/s angular=%.3f rad/s "
                "linear_accel=%.3f m/s^2 angular_accel=%.3f rad/s^2 "
                "wheel_cap=%.3f m/s inner_ratio=%.3f.",
                max_linear_speed_m_s_, max_angular_speed_rad_s_, max_linear_accel_m_s2_,
                max_angular_accel_rad_s2_, max_wheel_speed_m_s_, minimum_inner_wheel_ratio_);
    response.success = true;
    response.message = "Drive speed, acceleration, wheel cap, and differential ratio applied.";
  }

  void onSetMotorMapping(
      const crawling_robot_interfaces::srv::SetMotorMapping::Request& request,
      crawling_robot_interfaces::srv::SetMotorMapping::Response& response) {
    if (request.left_motor_id < 1 || request.left_motor_id > 32 ||
        request.right_motor_id < 1 || request.right_motor_id > 32 ||
        request.left_motor_id == request.right_motor_id ||
        (request.left_motor_sign != -1 && request.left_motor_sign != 1) ||
        (request.right_motor_sign != -1 && request.right_motor_sign != 1)) {
      response.success = false;
      response.message = "Motor IDs must be different values from 1 to 32 and signs must be -1 or 1.";
      return;
    }

    sendStops();
    drive_enabled_ = false;
    applied_linear_m_s_ = 0.0;
    applied_angular_rad_s_ = 0.0;
    left_motor_id_ = request.left_motor_id;
    right_motor_id_ = request.right_motor_id;
    left_motor_sign_ = request.left_motor_sign;
    right_motor_sign_ = request.right_motor_sign;
    left_feedback_ = WheelFeedback{};
    right_feedback_ = WheelFeedback{};
    stopped_ = true;
    RCLCPP_INFO(get_logger(), "Motor mapping applied: left=id%u sign=%+d, right=id%u sign=%+d.",
                static_cast<unsigned int>(left_motor_id_), left_motor_sign_,
                static_cast<unsigned int>(right_motor_id_), right_motor_sign_);
    response.success = true;
    response.message = "Motor mapping applied; drive output is disabled until explicitly enabled.";
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
      // Send the protocol's 0x81 hold command only on the transition into the
      // stopped state. Repeating it while idle floods the CAN bus.
      if (!stopped_) {
        sendStops();
        stopped_ = true;
      }
      applied_linear_m_s_ = 0.0;
      applied_angular_rad_s_ = 0.0;
      publishDriveCommandStatus(command, command_is_fresh, {});
      logDriveCommand(command, command_is_fresh, {});
      return;
    }

    const double target_linear = std::clamp(
        command.linear.x, -max_linear_speed_m_s_, max_linear_speed_m_s_);
    const double target_angular = std::clamp(
        command.angular.z, -max_angular_speed_rad_s_, max_angular_speed_rad_s_);
    // A keyboard release publishes an explicit zero command. Do not make the
    // operator wait for the acceleration ramp to decay: stop both wheels on
    // the next 20 ms control tick. A later command timeout remains the
    // fallback when a key-release event is lost.
    if (std::abs(target_linear) <= 1e-12 && std::abs(target_angular) <= 1e-12) {
      applied_linear_m_s_ = 0.0;
      applied_angular_rad_s_ = 0.0;
      if (!stopped_) {
        sendStops();
        stopped_ = true;
      }
      publishDriveCommandStatus(command, true, {});
      logDriveCommand(command, true, {});
      return;
    }
    // Ramp the linear/angular command as one vector. Ramping the two axes
    // independently can briefly reverse one wheel while an arc turn starts:
    // angular acceleration reaches its target before linear acceleration does.
    // A shared scale preserves the requested wheel-speed ratio at every 20 ms
    // step, so an arc command keeps both physical wheels moving in the same
    // direction from the first frame.
    const double linear_before = applied_linear_m_s_;
    const double angular_before = applied_angular_rad_s_;
    const double linear_delta = target_linear - linear_before;
    const double angular_delta = target_angular - angular_before;
    double ramp_scale = 1.0;
    if (!stopped_) {
      if (std::abs(linear_delta) > 1e-12) {
        ramp_scale = std::min(
            ramp_scale, max_linear_accel_m_s2_ * kControlPeriodSeconds /
                            std::abs(linear_delta));
      }
      if (std::abs(angular_delta) > 1e-12) {
        ramp_scale = std::min(
            ramp_scale, max_angular_accel_rad_s2_ * kControlPeriodSeconds /
                            std::abs(angular_delta));
      }
      ramp_scale = std::clamp(ramp_scale, 0.0, 1.0);
    }
    // The first command after a stop is an operator action and must reach both
    // motors immediately. Acceleration limiting remains active for subsequent
    // speed changes so continuous motion stays smooth.
    applied_linear_m_s_ += linear_delta * ramp_scale;
    applied_angular_rad_s_ += angular_delta * ramp_scale;

    RCLCPP_DEBUG_THROTTLE(
        get_logger(), *get_clock(), 200,
        "Drive ramp: target[v=%.4f,w=%.4f] before[v=%.4f,w=%.4f] "
        "after[v=%.4f,w=%.4f] delta[v=%.4f,w=%.4f] scale=%.4f "
        "accel[v=%.4f,w=%.4f].",
        target_linear, target_angular, linear_before, angular_before,
        applied_linear_m_s_, applied_angular_rad_s_, linear_delta, angular_delta,
        ramp_scale, max_linear_accel_m_s2_, max_angular_accel_rad_s2_);

    const auto wheel_speeds = mixDifferentialWheelSpeeds(
        applied_linear_m_s_, applied_angular_rad_s_, wheel_separation_m_,
        minimum_inner_wheel_ratio_);
    if (std::abs(wheel_speeds.applied_angular_rad_s - applied_angular_rad_s_) > 1e-9) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 1000,
          "Clamped angular command to keep translating wheels in the same physical direction.");
    }
    const auto limited_wheel_speeds = limitDifferentialWheelSpeeds(
        wheel_speeds, max_wheel_speed_m_s_);
    if (std::abs(limited_wheel_speeds.left_m_s - wheel_speeds.left_m_s) > 1e-9 ||
        std::abs(limited_wheel_speeds.right_m_s - wheel_speeds.right_m_s) > 1e-9) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 1000,
          "Wheel speed cap %.3f m/s active; preserving differential ratio.",
          max_wheel_speed_m_s_);
    }
    last_left_command_dps_ = wheelSpeedToMotorDps(
        limited_wheel_speeds.left_m_s, left_motor_sign_);
    last_right_command_dps_ = wheelSpeedToMotorDps(
        limited_wheel_speeds.right_m_s, right_motor_sign_);
    sendSpeed(left_motor_id_, limited_wheel_speeds.left_m_s, left_motor_sign_);
    sendSpeed(right_motor_id_, limited_wheel_speeds.right_m_s, right_motor_sign_);
    publishDriveCommandStatus(command, true, limited_wheel_speeds);
    logDriveCommand(command, true, limited_wheel_speeds);
    stopped_ = false;
  }

  bool hasValidGeometry() const {
    return wheel_radius_m_ > 0.0 && wheel_separation_m_ > 0.0 &&
           motor_output_to_wheel_ratio_ > 0.0 && max_wheel_speed_m_s_ > 0.0 &&
           std::isfinite(minimum_inner_wheel_ratio_) &&
           minimum_inner_wheel_ratio_ >= 0.0 && minimum_inner_wheel_ratio_ <= 1.0 &&
           validLimits(max_linear_speed_m_s_, max_angular_speed_rad_s_,
                       max_linear_accel_m_s2_, max_angular_accel_rad_s2_) &&
           left_motor_id_ >= 1 && left_motor_id_ <= 32 && right_motor_id_ >= 1 &&
           right_motor_id_ <= 32 && (left_motor_sign_ == -1 || left_motor_sign_ == 1) &&
           (right_motor_sign_ == -1 || right_motor_sign_ == 1);
  }

  static bool validLimits(double linear_speed, double angular_speed,
                          double linear_accel, double angular_accel) {
    return std::isfinite(linear_speed) && linear_speed > 0.0 &&
           linear_speed <= kMaxLinearSpeedMetersPerSecond &&
           std::isfinite(angular_speed) && angular_speed > 0.0 &&
           angular_speed <= kMaxAngularSpeedRadiansPerSecond &&
           std::isfinite(linear_accel) && linear_accel > 0.0 &&
           linear_accel <= kMaxLinearAccelMetersPerSecondSquared &&
           std::isfinite(angular_accel) && angular_accel > 0.0 &&
           angular_accel <= kMaxAngularAccelRadiansPerSecondSquared;
  }

  double wheelSpeedToMotorDps(double wheel_speed_m_s, int motor_sign) const {
    if (!(wheel_radius_m_ > 0.0) || !(motor_output_to_wheel_ratio_ > 0.0) ||
        !std::isfinite(wheel_speed_m_s)) {
      return 0.0;
    }
    const double bounded_speed = std::clamp(wheel_speed_m_s, -max_wheel_speed_m_s_, max_wheel_speed_m_s_);
    return bounded_speed / wheel_radius_m_ * kRadiansToDegrees *
           motor_output_to_wheel_ratio_ * static_cast<double>(motor_sign);
  }

  void sendSpeed(std::uint8_t motor_id, double wheel_speed_m_s, int motor_sign) {
    sendFrame(ServoV38::speedCommand(motor_id, wheelSpeedToMotorDps(wheel_speed_m_s, motor_sign)));
  }

  void sendStops() {
    sendFrame(ServoV38::stopCommand(left_motor_id_));
    sendFrame(ServoV38::stopCommand(right_motor_id_));
  }

  void sendFrame(const CanFrame& frame) {
    can_publisher_->publish(toRosFrame(frame, now()));
  }

  void publishDriveCommandStatus(const geometry_msgs::msg::Twist& target,
                                 bool command_is_fresh,
                                 const DifferentialWheelSpeeds& wheel_speeds) {
    crawling_robot_interfaces::msg::DriveCommandStatus status;
    status.drive_enabled = drive_enabled_;
    status.command_fresh = command_is_fresh;
    status.target_linear_m_s = target.linear.x;
    status.target_angular_rad_s = target.angular.z;
    // Wheel caps are applied uniformly. Derive the reported body speed from
    // the final CAN wheel commands so the monitor never reports a value that
    // was clipped before transmission.
    status.applied_linear_m_s = (wheel_speeds.left_m_s + wheel_speeds.right_m_s) * 0.5;
    status.applied_angular_rad_s = wheel_speeds.applied_angular_rad_s;
    status.left_wheel_m_s = wheel_speeds.left_m_s;
    status.right_wheel_m_s = wheel_speeds.right_m_s;
    command_status_publisher_->publish(status);
  }

  void logDriveCommand(const geometry_msgs::msg::Twist& target, bool command_is_fresh,
                       const DifferentialWheelSpeeds& wheel_speeds) {
    RCLCPP_INFO_THROTTLE(
        get_logger(), *get_clock(), 1000,
        "Drive command: enabled=%d fresh=%d target[v=%.4f m/s,w=%.4f rad/s] "
        "applied[v=%.4f m/s,w=%.4f rad/s] wheels[L=%.4f,R=%.4f m/s] "
        "motors[L:id%u sign=%+d dps=%.1f,R:id%u sign=%+d dps=%.1f].",
        drive_enabled_, command_is_fresh, target.linear.x, target.angular.z,
        (wheel_speeds.left_m_s + wheel_speeds.right_m_s) * 0.5,
        wheel_speeds.applied_angular_rad_s, wheel_speeds.left_m_s, wheel_speeds.right_m_s,
        static_cast<unsigned int>(left_motor_id_), left_motor_sign_,
        wheelSpeedToMotorDps(wheel_speeds.left_m_s, left_motor_sign_),
        static_cast<unsigned int>(right_motor_id_), right_motor_sign_,
        wheelSpeedToMotorDps(wheel_speeds.right_m_s, right_motor_sign_));
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
    WheelFeedback& wheel = is_left ? left_feedback_ : right_feedback_;
    wheel.valid = true;
    wheel.position = feedback->output_angle_deg / motor_output_to_wheel_ratio_ /
                     static_cast<double>(sign) / kRadiansToDegrees;
    wheel.velocity = feedback->output_speed_dps / motor_output_to_wheel_ratio_ /
                     static_cast<double>(sign) / kRadiansToDegrees;
    wheel.effort = feedback->torque_current_a;
    wheel.motor_speed_dps = feedback->output_speed_dps;
    wheel.received_at = now();
    logDriveFeedback();
    publishJointState();
  }

  void logDriveFeedback() {
    const double left_wheel_m_s = left_feedback_.velocity * wheel_radius_m_;
    const double right_wheel_m_s = right_feedback_.velocity * wheel_radius_m_;
    const auto timestamp = now();
    const double left_age_ms = left_feedback_.valid ?
        (timestamp - left_feedback_.received_at).seconds() * 1000.0 : -1.0;
    const double right_age_ms = right_feedback_.valid ?
        (timestamp - right_feedback_.received_at).seconds() * 1000.0 : -1.0;
    RCLCPP_INFO_THROTTLE(
        get_logger(), *get_clock(), 1000,
        "Drive feedback: left[valid=%d id%u target_dps=%.1f actual_dps=%.1f error_dps=%.1f "
        "wheel=%.4f m/s torque=%.2f A age=%.1f ms] right[valid=%d id%u target_dps=%.1f "
        "actual_dps=%.1f error_dps=%.1f wheel=%.4f m/s torque=%.2f A age=%.1f ms].",
        left_feedback_.valid, static_cast<unsigned int>(left_motor_id_),
        last_left_command_dps_, left_feedback_.motor_speed_dps,
        left_feedback_.motor_speed_dps - last_left_command_dps_, left_wheel_m_s,
        left_feedback_.effort, left_age_ms,
        right_feedback_.valid, static_cast<unsigned int>(right_motor_id_),
        last_right_command_dps_, right_feedback_.motor_speed_dps,
        right_feedback_.motor_speed_dps - last_right_command_dps_, right_wheel_m_s,
        right_feedback_.effort, right_age_ms);
  }

  void publishJointState() {
    sensor_msgs::msg::JointState state;
    state.header.stamp = now();
    if (left_feedback_.valid) {
      state.name.push_back("left_drive_wheel_joint");
      state.position.push_back(left_feedback_.position);
      state.velocity.push_back(left_feedback_.velocity);
      state.effort.push_back(left_feedback_.effort);
    }
    if (right_feedback_.valid) {
      state.name.push_back("right_drive_wheel_joint");
      state.position.push_back(right_feedback_.position);
      state.velocity.push_back(right_feedback_.velocity);
      state.effort.push_back(right_feedback_.effort);
    }
    if (state.name.empty()) return;
    joint_state_publisher_->publish(state);
  }

  struct WheelFeedback {
    bool valid = false;
    double position = 0.0;
    double velocity = 0.0;
    double effort = 0.0;
    double motor_speed_dps = 0.0;
    rclcpp::Time received_at{0, 0, RCL_ROS_TIME};
  };

  std::uint8_t left_motor_id_ = 1;
  std::uint8_t right_motor_id_ = 2;
  int left_motor_sign_ = 1;
  int right_motor_sign_ = -1;
  double wheel_radius_m_ = 0.0;
  double wheel_separation_m_ = 0.0;
  double motor_output_to_wheel_ratio_ = 1.0;
  double max_wheel_speed_m_s_ = 0.0;
  double minimum_inner_wheel_ratio_ = 0.5;
  double max_linear_speed_m_s_ = 0.0;
  double max_angular_speed_rad_s_ = 0.0;
  double max_linear_accel_m_s2_ = 0.0;
  double max_angular_accel_rad_s2_ = 0.0;
  double applied_linear_m_s_ = 0.0;
  double applied_angular_rad_s_ = 0.0;
  double last_left_command_dps_ = 0.0;
  double last_right_command_dps_ = 0.0;
  int command_timeout_ms_ = 200;
  std::string can_tx_topic_;
  std::string can_rx_topic_;
  bool drive_enabled_ = false;
  bool stopped_ = false;
  bool has_command_ = false;
  geometry_msgs::msg::Twist command_;
  rclcpp::Time last_command_time_{0, 0, RCL_ROS_TIME};
  std::mutex command_mutex_;
  WheelFeedback left_feedback_;
  WheelFeedback right_feedback_;
  rclcpp::Publisher<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joint_state_publisher_;
  rclcpp::Publisher<crawling_robot_interfaces::msg::DriveCommandStatus>::SharedPtr
      command_status_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_subscription_;
  rclcpp::Subscription<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_subscription_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr enable_service_;
  rclcpp::Service<crawling_robot_interfaces::srv::SetDriveLimits>::SharedPtr limits_service_;
  rclcpp::Service<crawling_robot_interfaces::srv::SetMotorMapping>::SharedPtr mapping_service_;
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
