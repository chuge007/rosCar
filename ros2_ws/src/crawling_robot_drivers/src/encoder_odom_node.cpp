#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "std_msgs/msg/int64.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2/utils.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_ros/transform_broadcaster.h"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {

class EncoderOdomNode final : public rclcpp::Node {
public:
  EncoderOdomNode() : Node("encoder_odom_node"), transform_broadcaster_(*this) {
    ticks_per_revolution_ = declare_parameter<double>("ticks_per_revolution", 4096.0);
    measuring_wheel_diameter_m_ = declare_parameter<double>("measuring_wheel_diameter_m", 0.05);
    encoder_sign_ = declare_parameter<int>("encoder_sign", 1);
    odom_frame_id_ = declare_parameter<std::string>("odom_frame_id", "odom");
    base_frame_id_ = declare_parameter<std::string>("base_frame_id", "base_link");

    odom_publisher_ = create_publisher<nav_msgs::msg::Odometry>("/odom", 20);
    encoder_subscription_ = create_subscription<std_msgs::msg::Int64>(
        "/scan_encoder/ticks", rclcpp::SensorDataQoS(),
        [this](std_msgs::msg::Int64::SharedPtr message) { onEncoderTicks(*message); });
    imu_subscription_ = create_subscription<sensor_msgs::msg::Imu>(
        "/imu/data", rclcpp::SensorDataQoS(),
        [this](sensor_msgs::msg::Imu::SharedPtr message) { onImu(*message); });
  }

private:
  void onImu(const sensor_msgs::msg::Imu& message) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    yaw_rad_ = tf2::getYaw(message.orientation);
    has_imu_ = true;
  }

  void onEncoderTicks(const std_msgs::msg::Int64& message) {
    if (ticks_per_revolution_ <= 0.0 || measuring_wheel_diameter_m_ <= 0.0) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000,
                            "Encoder calibration requires positive ticks_per_revolution and wheel diameter.");
      return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!has_encoder_) {
      last_ticks_ = message.data;
      last_tick_stamp_ = now();
      has_encoder_ = true;
      return;
    }

    const std::int64_t delta_ticks = (message.data - last_ticks_) * static_cast<std::int64_t>(encoder_sign_);
    last_ticks_ = message.data;
    const double meters_per_tick = 3.14159265358979323846 * measuring_wheel_diameter_m_ /
                                   ticks_per_revolution_;
    const auto stamp = now();
    const double distance = static_cast<double>(delta_ticks) * meters_per_tick;
    pose_x_m_ += distance * std::cos(yaw_rad_);
    pose_y_m_ += distance * std::sin(yaw_rad_);
    double linear_velocity_m_s = 0.0;
    if (last_tick_stamp_.nanoseconds() > 0) {
      const double elapsed_s = (stamp - last_tick_stamp_).seconds();
      if (elapsed_s > 0.0) {
        linear_velocity_m_s = distance / elapsed_s;
      }
    }
    last_tick_stamp_ = stamp;
    publishOdometry(stamp, linear_velocity_m_s);
  }

  void publishOdometry(const rclcpp::Time& stamp, double linear_velocity_m_s) {
    tf2::Quaternion orientation;
    orientation.setRPY(0.0, 0.0, yaw_rad_);

    nav_msgs::msg::Odometry odom;
    odom.header.stamp = stamp;
    odom.header.frame_id = odom_frame_id_;
    odom.child_frame_id = base_frame_id_;
    odom.pose.pose.position.x = pose_x_m_;
    odom.pose.pose.position.y = pose_y_m_;
    odom.pose.pose.orientation = tf2::toMsg(orientation);
    odom.twist.twist.linear.x = linear_velocity_m_s;
    odom.pose.covariance[0] = 0.02;
    odom.pose.covariance[7] = 0.02;
    odom.pose.covariance[35] = has_imu_ ? 0.04 : 1.0;
    odom_publisher_->publish(odom);

    geometry_msgs::msg::TransformStamped transform;
    transform.header = odom.header;
    transform.child_frame_id = base_frame_id_;
    transform.transform.translation.x = pose_x_m_;
    transform.transform.translation.y = pose_y_m_;
    transform.transform.rotation = odom.pose.pose.orientation;
    transform_broadcaster_.sendTransform(transform);
  }

  double ticks_per_revolution_ = 4096.0;
  double measuring_wheel_diameter_m_ = 0.05;
  int encoder_sign_ = 1;
  std::string odom_frame_id_;
  std::string base_frame_id_;
  bool has_imu_ = false;
  bool has_encoder_ = false;
  std::int64_t last_ticks_ = 0;
  rclcpp::Time last_tick_stamp_{0, 0, RCL_ROS_TIME};
  double yaw_rad_ = 0.0;
  double pose_x_m_ = 0.0;
  double pose_y_m_ = 0.0;
  std::mutex state_mutex_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_publisher_;
  rclcpp::Subscription<std_msgs::msg::Int64>::SharedPtr encoder_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_subscription_;
  tf2_ros::TransformBroadcaster transform_broadcaster_;
};

}  // namespace crawling_robot_drivers

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("encoder_odom_node");
  rclcpp::spin(std::make_shared<crawling_robot_drivers::EncoderOdomNode>());
  rclcpp::shutdown();
  return 0;
}
