#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "trajectory_msgs/msg/joint_trajectory.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {

class LinearAxisNode final : public rclcpp::Node {
public:
  LinearAxisNode() : Node("linear_axis_node") {
    axis_names_ = declare_parameter<std::vector<std::string>>(
        "axis_names", {"inspection_slide_joint", "probe_slide_joint", "probe_clamp_joint"});
    lower_limits_m_ = declare_parameter<std::vector<double>>("lower_limits_m", {0.0, 0.0, 0.0});
    upper_limits_m_ = declare_parameter<std::vector<double>>("upper_limits_m", {0.0, 0.0, 0.0});
    max_velocities_m_s_ = declare_parameter<std::vector<double>>("max_velocities_m_s", {0.0, 0.0, 0.0});
    max_accelerations_m_s2_ =
        declare_parameter<std::vector<double>>("max_accelerations_m_s2", {0.0, 0.0, 0.0});
    dry_run_ = declare_parameter<bool>("dry_run", true);
    protocol_verified_ = declare_parameter<bool>("protocol_verified", false);

    state_publisher_ = create_publisher<sensor_msgs::msg::JointState>("/scan_axes/joint_states", 20);
    command_subscription_ = create_subscription<trajectory_msgs::msg::JointTrajectory>(
        "/scan_axes/command", 10,
        [this](trajectory_msgs::msg::JointTrajectory::SharedPtr message) { onCommand(*message); });
    enable_service_ = create_service<std_srvs::srv::SetBool>(
        "/scan_axes/enable",
        [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
               std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
          if (request->data && (!protocol_verified_ || dry_run_)) {
            response->success = false;
            response->message = "A verified linear-axis protocol adapter is required before real motion is enabled.";
            return;
          }
          enabled_ = request->data;
          response->success = true;
          response->message = enabled_ ? "Linear-axis output enabled." : "Linear-axis output disabled.";
        });
  }

private:
  void onCommand(const trajectory_msgs::msg::JointTrajectory& trajectory) {
    if (!hasValidAxisConfiguration()) {
      RCLCPP_ERROR(get_logger(), "Configure exactly three scan-axis names, travel limits, speed, and acceleration limits.");
      return;
    }
    if (trajectory.joint_names != axis_names_ || trajectory.points.empty()) {
      RCLCPP_ERROR(get_logger(), "Expected one non-empty trajectory for the configured three scan axes.");
      return;
    }

    const auto& target = trajectory.points.back();
    if (target.positions.size() != axis_names_.size()) {
      RCLCPP_ERROR(get_logger(), "The scan-axis trajectory must provide a position for every axis.");
      return;
    }
    if ((!target.velocities.empty() && target.velocities.size() != axis_names_.size()) ||
        (!target.accelerations.empty() && target.accelerations.size() != axis_names_.size())) {
      RCLCPP_ERROR(get_logger(), "Provided scan-axis velocity and acceleration arrays must cover every axis.");
      return;
    }
    for (std::size_t index = 0; index < target.positions.size(); ++index) {
      const double position = target.positions[index];
      if (!std::isfinite(position) || position < lower_limits_m_[index] || position > upper_limits_m_[index]) {
        RCLCPP_ERROR(get_logger(), "Axis %s target %.6f m violates its configured travel range.",
                     axis_names_[index].c_str(), position);
        return;
      }
      if (!target.velocities.empty() &&
          (!std::isfinite(target.velocities[index]) ||
           std::abs(target.velocities[index]) > max_velocities_m_s_[index])) {
        RCLCPP_ERROR(get_logger(), "Axis %s velocity violates its configured limit.",
                     axis_names_[index].c_str());
        return;
      }
      if (!target.accelerations.empty() &&
          (!std::isfinite(target.accelerations[index]) ||
           std::abs(target.accelerations[index]) > max_accelerations_m_s2_[index])) {
        RCLCPP_ERROR(get_logger(), "Axis %s acceleration violates its configured limit.",
                     axis_names_[index].c_str());
        return;
      }
    }

    if (!enabled_) {
      RCLCPP_WARN(get_logger(), "Rejected scan-axis command because output is disabled.");
      return;
    }
    if (!protocol_verified_ || dry_run_) {
      RCLCPP_WARN(get_logger(), "Accepted scan-axis trajectory only in dry-run mode; no motor frame was sent.");
      publishState(target);
      return;
    }

    RCLCPP_ERROR(get_logger(), "No linear-axis protocol adapter has been installed.");
  }

  bool hasValidAxisConfiguration() const {
    if (axis_names_.size() != 3 || lower_limits_m_.size() != axis_names_.size() ||
        upper_limits_m_.size() != axis_names_.size() || max_velocities_m_s_.size() != axis_names_.size() ||
        max_accelerations_m_s2_.size() != axis_names_.size()) {
      return false;
    }
    for (std::size_t index = 0; index < axis_names_.size(); ++index) {
      if (axis_names_[index].empty() || !std::isfinite(lower_limits_m_[index]) ||
          !std::isfinite(upper_limits_m_[index]) || lower_limits_m_[index] > upper_limits_m_[index] ||
          !std::isfinite(max_velocities_m_s_[index]) || max_velocities_m_s_[index] <= 0.0 ||
          !std::isfinite(max_accelerations_m_s2_[index]) || max_accelerations_m_s2_[index] <= 0.0) {
        return false;
      }
    }
    return true;
  }

  void publishState(const trajectory_msgs::msg::JointTrajectoryPoint& point) {
    sensor_msgs::msg::JointState state;
    state.header.stamp = now();
    state.name = axis_names_;
    state.position = point.positions;
    if (point.velocities.size() == axis_names_.size()) {
      state.velocity = point.velocities;
    }
    state_publisher_->publish(state);
  }

  std::vector<std::string> axis_names_;
  std::vector<double> lower_limits_m_;
  std::vector<double> upper_limits_m_;
  std::vector<double> max_velocities_m_s_;
  std::vector<double> max_accelerations_m_s2_;
  bool dry_run_ = true;
  bool protocol_verified_ = false;
  bool enabled_ = false;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr state_publisher_;
  rclcpp::Subscription<trajectory_msgs::msg::JointTrajectory>::SharedPtr command_subscription_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr enable_service_;
};

}  // namespace crawling_robot_drivers

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("linear_axis_node");
  rclcpp::spin(std::make_shared<crawling_robot_drivers::LinearAxisNode>());
  rclcpp::shutdown();
  return 0;
}
