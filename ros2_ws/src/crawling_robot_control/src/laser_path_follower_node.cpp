#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "std_srvs/srv/trigger.hpp"

#include "crawling_robot_interfaces/msg/laser_correction_status.hpp"
#include "crawling_robot_interfaces/msg/laser_profile.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_control {
namespace {

struct Sample {
  double forward = 0.0;
  double lateral = 0.0;
  double height = 0.0;
};

struct ContourMeasurement {
  double forward = 0.0;
  double lateral = 0.0;
  double confidence = 0.0;
};

struct OdomState {
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
  rclcpp::Time stamp{0, 0, RCL_ROS_TIME};
};

struct TrajectorySample {
  double x = 0.0;
  double y = 0.0;
  double confidence = 0.0;
  rclcpp::Time stamp{0, 0, RCL_ROS_TIME};
};

struct GeometryEstimate {
  bool valid = false;
  double lateral_error = 0.0;
  double preview_lateral_error = 0.0;
  double heading_error = 0.0;
  double curvature = 0.0;
  double residual = 0.0;
  std::size_t points = 0;
};

double clampValue(double value, double low, double high) {
  return std::max(low, std::min(value, high));
}

bool readPointScalar(const sensor_msgs::msg::PointCloud2& cloud, std::size_t offset,
                    std::uint8_t datatype, double& value) {
  const std::size_t scalar_size = datatype == sensor_msgs::msg::PointField::FLOAT32
                                      ? sizeof(float)
                                      : datatype == sensor_msgs::msg::PointField::FLOAT64
                                            ? sizeof(double)
                                            : 0;
  if (scalar_size == 0 || offset > cloud.data.size() || scalar_size > cloud.data.size() - offset) {
    return false;
  }

  // PointCloud2 carries an explicit byte-order flag. The supported targets are
  // little-endian, but handling a big-endian packet here avoids silently
  // turning valid profile values into very large or non-finite numbers.
  std::uint8_t bytes[sizeof(double)] = {};
  std::memcpy(bytes, cloud.data.data() + offset, scalar_size);
  const std::uint16_t marker = 0x0001;
  const bool host_big_endian =
      *reinterpret_cast<const std::uint8_t*>(&marker) == 0;
  if (static_cast<bool>(cloud.is_bigendian) != host_big_endian) {
    std::reverse(bytes, bytes + scalar_size);
  }
  if (datatype == sensor_msgs::msg::PointField::FLOAT32) {
    float scalar = 0.0F;
    std::memcpy(&scalar, bytes, sizeof(scalar));
    value = static_cast<double>(scalar);
  } else {
    std::memcpy(&value, bytes, sizeof(value));
  }
  return std::isfinite(value);
}

bool findField(const sensor_msgs::msg::PointCloud2& cloud, const std::string& name,
               std::uint32_t& offset, std::uint8_t& datatype) {
  for (const auto& field : cloud.fields) {
    if (field.name == name) {
      offset = field.offset;
      datatype = field.datatype;
      return true;
    }
  }
  return false;
}

double wrapAngle(double angle) {
  while (angle > 3.14159265358979323846) {
    angle -= 2.0 * 3.14159265358979323846;
  }
  while (angle < -3.14159265358979323846) {
    angle += 2.0 * 3.14159265358979323846;
  }
  return angle;
}

double interpolateAngle(double first, double second, double ratio) {
  return wrapAngle(first + ratio * wrapAngle(second - first));
}

bool solve3x3(double matrix[3][3], double rhs[3], double solution[3]) {
  for (int column = 0; column < 3; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 3; ++row) {
      if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][column]) < 1e-12) {
      return false;
    }
    if (pivot != column) {
      for (int entry = column; entry < 3; ++entry) {
        std::swap(matrix[column][entry], matrix[pivot][entry]);
      }
      std::swap(rhs[column], rhs[pivot]);
    }
    const double divisor = matrix[column][column];
    for (int entry = column; entry < 3; ++entry) {
      matrix[column][entry] /= divisor;
    }
    rhs[column] /= divisor;
    for (int row = 0; row < 3; ++row) {
      if (row == column) {
        continue;
      }
      const double factor = matrix[row][column];
      for (int entry = column; entry < 3; ++entry) {
        matrix[row][entry] -= factor * matrix[column][entry];
      }
      rhs[row] -= factor * rhs[column];
    }
  }
  for (int index = 0; index < 3; ++index) {
    solution[index] = rhs[index];
  }
  return true;
}

std::vector<double> medianSmooth(const std::vector<double>& input, int window) {
  if (input.empty() || window <= 1) {
    return input;
  }
  if ((window % 2) == 0) {
    ++window;
  }
  const int radius = window / 2;
  std::vector<double> output(input.size());
  std::vector<double> values;
  values.reserve(static_cast<std::size_t>(window));
  for (std::size_t index = 0; index < input.size(); ++index) {
    values.clear();
    const std::size_t first = index > static_cast<std::size_t>(radius)
                                  ? index - static_cast<std::size_t>(radius)
                                  : 0;
    const std::size_t last = std::min(input.size() - 1, index + static_cast<std::size_t>(radius));
    for (std::size_t sample = first; sample <= last; ++sample) {
      values.push_back(input[sample]);
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    output[index] = *middle;
  }
  return output;
}

}  // namespace

class LaserPathFollowerNode final : public rclcpp::Node {
public:
  LaserPathFollowerNode() : Node("laser_path_follower_node") {
    std::fprintf(stderr, "[laser_path_follower_node] ctor start\n");
    std::fflush(stderr);
    profile_topic_ = declare_parameter<std::string>("profile_topic", "/laser_profile/frame");
    odom_topic_ = declare_parameter<std::string>("odom_topic", "/odom");
    cmd_vel_topic_ = declare_parameter<std::string>("cmd_vel_topic", "/cmd_vel");
    status_topic_ = declare_parameter<std::string>("status_topic", "/laser_correction/status");
    lateral_axis_ = declare_parameter<std::string>("lateral_axis", "y");
    height_axis_ = declare_parameter<std::string>("height_axis", "z");
    forward_axis_ = declare_parameter<std::string>("forward_axis", "x");
    forward_sign_ = declare_parameter<int>("forward_sign", 1);
    lateral_sign_ = declare_parameter<int>("lateral_sign", 1);
    height_sign_ = declare_parameter<int>("height_sign", 1);
    lateral_min_m_ = declare_parameter<double>("lateral_min_m", -0.20);
    lateral_max_m_ = declare_parameter<double>("lateral_max_m", 0.20);
    target_lateral_m_ = declare_parameter<double>("target_lateral_m", 0.0);
    prominence_threshold_m_ = declare_parameter<double>("prominence_threshold_m", 0.0015);
    confidence_scale_m_ = declare_parameter<double>("confidence_scale_m", 0.004);
    min_candidate_points_ = declare_parameter<int>("min_candidate_points", 3);
    baseline_fraction_ = declare_parameter<double>("baseline_fraction", 0.60);
    median_window_points_ = declare_parameter<int>("median_window_points", 5);
    contour_half_width_m_ = declare_parameter<double>("contour_half_width_m", 0.025);
    filter_alpha_ = declare_parameter<double>("filter_alpha", 0.22);
    derivative_alpha_ = declare_parameter<double>("derivative_alpha", 0.18);
    kp_ = declare_parameter<double>("kp", 3.0);
    kd_ = declare_parameter<double>("kd", 0.35);
    steering_sign_ = declare_parameter<int>("steering_sign", 1);
    deadband_m_ = declare_parameter<double>("deadband_m", 0.0015);
    max_lateral_error_m_ = declare_parameter<double>("max_lateral_error_m", 0.08);
    target_speed_m_s_ = declare_parameter<double>("target_speed_m_s", 0.025);
    min_speed_scale_ = declare_parameter<double>("min_speed_scale", 0.35);
    speed_reduction_gain_ = declare_parameter<double>("speed_reduction_gain", 0.8);
    max_angular_z_rad_s_ = declare_parameter<double>("max_angular_z_rad_s", 0.45);
    max_angular_accel_rad_s2_ = declare_parameter<double>("max_angular_accel_rad_s2", 0.9);
    max_linear_accel_m_s2_ = declare_parameter<double>("max_linear_accel_m_s2", 0.05);
    control_period_ms_ = declare_parameter<int>("control_period_ms", 20);
    profile_timeout_ms_ = declare_parameter<int>("profile_timeout_ms", 180);
    laser_forward_m_ = declare_parameter<double>("laser_forward_m", 0.0);
    laser_lateral_offset_m_ = declare_parameter<double>("laser_lateral_offset_m", 0.0);
    laser_yaw_rad_ = declare_parameter<double>("laser_yaw_rad", 0.0);
    trajectory_window_m_ = declare_parameter<double>("trajectory_window_m", 0.12);
    max_trajectory_points_ = declare_parameter<int>("max_trajectory_points", 80);
    min_fit_points_ = declare_parameter<int>("min_fit_points", 6);
    fit_huber_delta_m_ = declare_parameter<double>("fit_huber_delta_m", 0.006);
    max_fit_residual_m_ = declare_parameter<double>("max_fit_residual_m", 0.012);
    geometry_filter_alpha_ = declare_parameter<double>("geometry_filter_alpha", 0.25);
    heading_gain_ = declare_parameter<double>("heading_gain", 1.6);
    max_odom_age_ms_ = declare_parameter<int>("max_odom_age_ms", 120);
    min_fit_span_m_ = declare_parameter<double>("min_fit_span_m", 0.03);
    max_heading_error_rad_ = declare_parameter<double>("max_heading_error_rad", 0.785);
    preview_distance_m_ = declare_parameter<double>("preview_distance_m", 0.06);
    preview_filter_alpha_ = declare_parameter<double>("preview_filter_alpha", 0.25);
    curvature_feedforward_gain_ = declare_parameter<double>("curvature_feedforward_gain", 1.0);
    lateral_preview_gain_ = declare_parameter<double>("lateral_preview_gain", 2.4);
    integral_gain_ = declare_parameter<double>("integral_gain", 0.0);
    integral_limit_m_s_ = declare_parameter<double>("integral_limit_m_s", 0.03);
    integral_heading_gate_rad_ = declare_parameter<double>("integral_heading_gate_rad", 0.0524);
    integral_error_gate_m_ = declare_parameter<double>("integral_error_gate_m", 0.03);
    max_angular_jerk_rad_s3_ = declare_parameter<double>("max_angular_jerk_rad_s3", 2.0);
    max_curvature_1pm_ = declare_parameter<double>("max_curvature_1pm", 8.0);
    std::fprintf(stderr, "[laser_path_follower_node] parameters ready\n");
    std::fflush(stderr);

    profile_subscription_ = create_subscription<crawling_robot_interfaces::msg::LaserProfile>(
        profile_topic_, rclcpp::SensorDataQoS(),
        [this](crawling_robot_interfaces::msg::LaserProfile::SharedPtr message) {
          onProfile(*message);
        });
    odom_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
        odom_topic_, rclcpp::SensorDataQoS(),
        [this](nav_msgs::msg::Odometry::SharedPtr message) { onOdom(*message); });
    cmd_vel_publisher_ = create_publisher<geometry_msgs::msg::Twist>(cmd_vel_topic_, 20);
    status_publisher_ = create_publisher<crawling_robot_interfaces::msg::LaserCorrectionStatus>(
        status_topic_, 20);
    std::fprintf(stderr, "[laser_path_follower_node] pub/sub ready\n");
    std::fflush(stderr);
    enable_service_ = create_service<std_srvs::srv::SetBool>(
        "/laser_correction/enable",
        [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
               std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
          setEnabled(request->data, *response);
        });
    reset_service_ = create_service<std_srvs::srv::Trigger>(
        "/laser_correction/reset",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          resetController();
          response->success = true;
          response->message = "Laser correction filters and command ramp reset.";
        });
    control_timer_ = create_wall_timer(std::chrono::milliseconds(std::max(1, control_period_ms_)),
                                       [this] { updateCommand(); });
    status_timer_ = create_wall_timer(std::chrono::milliseconds(100), [this] { publishStatus(); });
    std::fprintf(stderr, "[laser_path_follower_node] services/timers ready\n");
    std::fflush(stderr);

    RCLCPP_INFO(get_logger(),
                "Laser path follower ready. Enable with /laser_correction/enable; steering_sign=%d.",
                steering_sign_);
  }

private:
  void setEnabled(bool enabled, std_srvs::srv::SetBool::Response& response) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      active_ = enabled;
      if (enabled) {
        resetControllerLocked();
      } else {
        target_linear_m_s_ = 0.0;
        target_angular_rad_s_ = 0.0;
        current_linear_m_s_ = 0.0;
        current_angular_rad_s_ = 0.0;
        current_angular_accel_rad_s2_ = 0.0;
        linear_command_m_s_ = 0.0;
        angular_command_rad_s_ = 0.0;
        angular_accel_rad_s2_ = 0.0;
        preview_integral_m_s_ = 0.0;
      }
    }
    if (!enabled) {
      publishZero();
    }
    response.success = true;
    response.message = enabled ? "Automatic laser correction enabled." :
                                 "Automatic laser correction disabled.";
  }

  void resetController() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    resetControllerLocked();
  }

  void resetControllerLocked() {
    filtered_error_m_ = 0.0;
    filtered_derivative_m_s_ = 0.0;
    filtered_preview_error_m_ = 0.0;
    preview_integral_m_s_ = 0.0;
    target_linear_m_s_ = 0.0;
    target_angular_rad_s_ = 0.0;
    current_linear_m_s_ = 0.0;
    current_angular_rad_s_ = 0.0;
    current_angular_accel_rad_s2_ = 0.0;
    has_previous_error_ = false;
    contour_valid_ = false;
    confidence_ = 0.0;
    geometry_valid_ = false;
    filtered_heading_error_ = 0.0;
    heading_error_rad_ = 0.0;
    preview_lateral_error_m_ = 0.0;
    curvature_1pm_ = 0.0;
    angular_accel_rad_s2_ = 0.0;
    fit_residual_m_ = 0.0;
    trajectory_points_ = 0;
    trajectory_.clear();
  }

  bool extractContour(const sensor_msgs::msg::PointCloud2& cloud, ContourMeasurement& measurement) const {
    if (cloud.point_step == 0 || cloud.width == 0 || cloud.height == 0) {
      return false;
    }
    std::uint32_t lateral_offset = 0;
    std::uint32_t height_offset = 0;
    std::uint32_t forward_offset = 0;
    std::uint8_t lateral_type = 0;
    std::uint8_t height_type = 0;
    std::uint8_t forward_type = 0;
    if (!findField(cloud, lateral_axis_, lateral_offset, lateral_type) ||
        !findField(cloud, height_axis_, height_offset, height_type) ||
        !findField(cloud, forward_axis_, forward_offset, forward_type)) {
      return false;
    }

    std::vector<Sample> samples;
    const std::size_t rows = cloud.height;
    const std::size_t columns = cloud.width;
    samples.reserve(rows * columns);
    const std::size_t row_step = cloud.row_step > 0 ? cloud.row_step : columns * cloud.point_step;
    for (std::size_t row = 0; row < rows; ++row) {
      for (std::size_t column = 0; column < columns; ++column) {
        const std::size_t base = row * row_step + column * cloud.point_step;
        double x = 0.0;
        double z = 0.0;
        double forward = 0.0;
        if (!readPointScalar(cloud, base + lateral_offset, lateral_type, x) ||
            !readPointScalar(cloud, base + height_offset, height_type, z) ||
            !readPointScalar(cloud, base + forward_offset, forward_type, forward)) {
          continue;
        }
        const double signed_forward = forward * static_cast<double>(forward_sign_ == -1 ? -1 : 1);
        const double signed_lateral = x * static_cast<double>(lateral_sign_ == -1 ? -1 : 1);
        const double signed_height = z * static_cast<double>(height_sign_ == -1 ? -1 : 1);
        const double lateral_low = std::min(lateral_min_m_, lateral_max_m_);
        const double lateral_high = std::max(lateral_min_m_, lateral_max_m_);
        if (signed_lateral < lateral_low || signed_lateral > lateral_high) {
          continue;
        }
        samples.push_back({signed_forward, signed_lateral, signed_height});
      }
    }
    if (samples.size() < static_cast<std::size_t>(std::max(3, min_candidate_points_))) {
      return false;
    }
    std::sort(samples.begin(), samples.end(), [](const Sample& first, const Sample& second) {
      return first.lateral < second.lateral;
    });
    std::vector<double> heights;
    heights.reserve(samples.size());
    for (const auto& sample : samples) {
      heights.push_back(sample.height);
    }
    heights = medianSmooth(heights, median_window_points_);

    // Estimate the wall plane from the low 60% of points so a narrow raised seam
    // cannot pull the baseline upward.
    std::vector<std::size_t> by_height(samples.size());
    for (std::size_t i = 0; i < by_height.size(); ++i) {
      by_height[i] = i;
    }
    std::sort(by_height.begin(), by_height.end(), [&heights](std::size_t first, std::size_t second) {
      return heights[first] < heights[second];
    });
    const double fraction = clampValue(baseline_fraction_, 0.2, 0.9);
    const std::size_t baseline_count = std::max<std::size_t>(2, static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(samples.size()))));
    double sum_x = 0.0;
    double sum_z = 0.0;
    double sum_xx = 0.0;
    double sum_xz = 0.0;
    for (std::size_t i = 0; i < std::min(baseline_count, by_height.size()); ++i) {
      const auto& sample = samples[by_height[i]];
      sum_x += sample.lateral;
      sum_z += heights[by_height[i]];
      sum_xx += sample.lateral * sample.lateral;
      sum_xz += sample.lateral * heights[by_height[i]];
    }
    const double count = static_cast<double>(std::min(baseline_count, by_height.size()));
    const double denominator = count * sum_xx - sum_x * sum_x;
    const double slope = std::abs(denominator) > 1e-12 ? (count * sum_xz - sum_x * sum_z) / denominator : 0.0;
    const double intercept = (sum_z - slope * sum_x) / count;

    std::vector<double> prominence(samples.size(), 0.0);
    std::size_t peak_index = 0;
    double peak_prominence = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
      prominence[i] = heights[i] - (slope * samples[i].lateral + intercept);
      if (prominence[i] > peak_prominence) {
        peak_prominence = prominence[i];
        peak_index = i;
      }
    }
    if (!std::isfinite(peak_prominence) || peak_prominence < prominence_threshold_m_) {
      return false;
    }

    double weighted_lateral = 0.0;
    double weighted_forward = 0.0;
    double weight_sum = 0.0;
    int candidate_count = 0;
    const double half_width = std::max(0.001, contour_half_width_m_);
    const double minimum_prominence = std::max(prominence_threshold_m_, peak_prominence * 0.35);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      if (std::abs(samples[i].lateral - samples[peak_index].lateral) <= half_width &&
          prominence[i] >= minimum_prominence) {
        const double weight = prominence[i] - minimum_prominence + 1e-9;
        weighted_lateral += samples[i].lateral * weight;
        weighted_forward += samples[i].forward * weight;
        weight_sum += weight;
        ++candidate_count;
      }
    }
    if (candidate_count < min_candidate_points_ || weight_sum <= 0.0) {
      return false;
    }
    measurement.lateral = weighted_lateral / weight_sum;
    measurement.forward = weighted_forward / weight_sum;
    const double strength = clampValue(peak_prominence / std::max(confidence_scale_m_, 1e-6), 0.0, 1.0);
    const double density = clampValue(static_cast<double>(candidate_count) /
                                          static_cast<double>(std::max(1, min_candidate_points_ * 3)),
                                      0.0, 1.0);
    measurement.confidence = strength * density;
    return std::isfinite(measurement.lateral) && std::isfinite(measurement.forward) &&
           std::isfinite(measurement.confidence);
  }

  void onOdom(const nav_msgs::msg::Odometry& message) {
    OdomState state;
    state.x = message.pose.pose.position.x;
    state.y = message.pose.pose.position.y;
    const auto& q = message.pose.pose.orientation;
    state.yaw = std::atan2(2.0 * (q.w * q.z + q.x * q.y),
                           1.0 - 2.0 * (q.y * q.y + q.z * q.z));
    state.stamp = rclcpp::Time(message.header.stamp);
    if (state.stamp.nanoseconds() == 0) {
      state.stamp = now();
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    odom_history_.push_back(state);
    while (odom_history_.size() > 100) {
      odom_history_.pop_front();
    }
  }

  bool interpolateOdom(const rclcpp::Time& stamp, OdomState& result) const {
    if (odom_history_.empty()) {
      return false;
    }
    if (stamp <= odom_history_.front().stamp) {
      result = odom_history_.front();
      return (result.stamp - stamp).seconds() * 1000.0 <=
             static_cast<double>(std::max(20, max_odom_age_ms_));
    }
    if (stamp >= odom_history_.back().stamp) {
      result = odom_history_.back();
      return (stamp - result.stamp).seconds() * 1000.0 <= static_cast<double>(std::max(20, max_odom_age_ms_));
    }
    for (std::size_t index = 1; index < odom_history_.size(); ++index) {
      const auto& first = odom_history_[index - 1];
      const auto& second = odom_history_[index];
      if (stamp > second.stamp) {
        continue;
      }
      const double span = (second.stamp - first.stamp).seconds();
      const double ratio = span > 1e-6 ? clampValue((stamp - first.stamp).seconds() / span, 0.0, 1.0) : 0.0;
      result.x = first.x + ratio * (second.x - first.x);
      result.y = first.y + ratio * (second.y - first.y);
      result.yaw = interpolateAngle(first.yaw, second.yaw, ratio);
      result.stamp = stamp;
      return std::abs((stamp - result.stamp).seconds()) * 1000.0 <=
             static_cast<double>(std::max(20, max_odom_age_ms_));
    }
    return false;
  }

  GeometryEstimate estimateGeometry(const rclcpp::Time& stamp, const ContourMeasurement& measurement) {
    GeometryEstimate estimate;
    OdomState current_odom;
    if (!interpolateOdom(stamp, current_odom)) {
      return estimate;
    }
    const double c = std::cos(current_odom.yaw);
    const double s = std::sin(current_odom.yaw);
    const double laser_c = std::cos(laser_yaw_rad_);
    const double laser_s = std::sin(laser_yaw_rad_);
    const double base_x = laser_forward_m_ + laser_c * measurement.forward - laser_s * measurement.lateral;
    const double base_y = laser_lateral_offset_m_ + laser_s * measurement.forward + laser_c * measurement.lateral;
    const double world_x = current_odom.x + c * base_x - s * base_y;
    const double world_y = current_odom.y + s * base_x + c * base_y;
    trajectory_.push_back({world_x, world_y, measurement.confidence, stamp});
    const double window = std::max(0.02, trajectory_window_m_);
    while (!trajectory_.empty()) {
      const double dx = world_x - trajectory_.front().x;
      const double dy = world_y - trajectory_.front().y;
      if (std::hypot(dx, dy) <= window &&
          trajectory_.size() <= static_cast<std::size_t>(std::max(8, max_trajectory_points_))) {
        break;
      }
      trajectory_.pop_front();
    }
    estimate.points = trajectory_.size();
    if (trajectory_.size() < static_cast<std::size_t>(std::max(6, min_fit_points_))) {
      return estimate;
    }

    // Express the accumulated path in the current vehicle frame. A local
    // quadratic preserves curvature and gives a well-defined tangent at the
    // closest point, unlike a two-frame slope estimate.
    struct LocalPoint { double x; double y; double base_weight; };
    std::vector<LocalPoint> points;
    points.reserve(trajectory_.size());
    for (const auto& point : trajectory_) {
      const double dx = point.x - current_odom.x;
      const double dy = point.y - current_odom.y;
      points.push_back({c * dx + s * dy, -s * dx + c * dy,
                        clampValue(point.confidence, 0.05, 1.0)});
    }
    std::vector<double> weights(points.size(), 0.0);
    double coefficients[3] = {0.0, 0.0, 0.0};
    const double fit_scale = std::max(0.02, trajectory_window_m_);
    double residual_sum = std::numeric_limits<double>::infinity();
    double residual_weight = 0.0;
    for (int iteration = 0; iteration < 4; ++iteration) {
      double normal[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
      double rhs[3] = {0.0, 0.0, 0.0};
      for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        double weight = point.base_weight;
        if (iteration > 0) {
          const double u = point.x / fit_scale;
          const double predicted = coefficients[0] * u * u + coefficients[1] * u + coefficients[2];
          const double residual = std::abs(point.y - predicted);
          const double delta = std::max(0.001, fit_huber_delta_m_);
          weight *= residual <= delta ? 1.0 : delta / residual;
        }
        weights[index] = weight;
        const double u = point.x / fit_scale;
        const double basis[3] = {u * u, u, 1.0};
        for (int row = 0; row < 3; ++row) {
          rhs[row] += weight * basis[row] * point.y;
          for (int column = 0; column < 3; ++column) {
            normal[row][column] += weight * basis[row] * basis[column];
          }
        }
      }
      if (!solve3x3(normal, rhs, coefficients)) {
        return estimate;
      }
      residual_sum = 0.0;
      residual_weight = 0.0;
      for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        const double u = point.x / fit_scale;
        const double predicted = coefficients[0] * u * u + coefficients[1] * u + coefficients[2];
        residual_sum += weights[index] * std::abs(point.y - predicted);
        residual_weight += weights[index];
      }
    }
    estimate.residual = residual_weight > 1e-6 ? residual_sum / residual_weight : std::numeric_limits<double>::infinity();
    if (!std::isfinite(estimate.residual) || estimate.residual > std::max(0.001, max_fit_residual_m_)) {
      return estimate;
    }

    // Find the closest point on y=f(x) to the vehicle reference point (0, 0).
    // Newton iterations solve x + f(x)f'(x) = 0 and remain inside the fitted window.
    double closest_x = 0.0;
    double min_x = points.front().x;
    double max_x = points.front().x;
    for (const auto& point : points) {
      min_x = std::min(min_x, point.x);
      max_x = std::max(max_x, point.x);
    }
    if (max_x - min_x < std::max(0.01, min_fit_span_m_)) {
      return estimate;
    }
    min_x = std::min(0.0, min_x);
    max_x = std::max(0.0, max_x);
    closest_x = clampValue(closest_x, min_x, max_x);
    for (int iteration = 0; iteration < 8; ++iteration) {
      const double u = closest_x / fit_scale;
      const double path_y = coefficients[0] * u * u + coefficients[1] * u + coefficients[2];
      const double slope = (2.0 * coefficients[0] * u + coefficients[1]) / fit_scale;
      const double derivative = closest_x + path_y * slope;
      const double second = 1.0 + slope * slope +
                            path_y * 2.0 * coefficients[0] / (fit_scale * fit_scale);
      if (std::abs(second) < 1e-9) {
        break;
      }
      closest_x = clampValue(closest_x - derivative / second, min_x, max_x);
    }
    const double closest_u = closest_x / fit_scale;
    const double closest_y = coefficients[0] * closest_u * closest_u +
                             coefficients[1] * closest_u + coefficients[2];
    const double tangent_slope = (2.0 * coefficients[0] * closest_u + coefficients[1]) / fit_scale;
    const double normal_scale = std::sqrt(1.0 + tangent_slope * tangent_slope);
    // Positive means the seam is to the vehicle's left. Subtract the desired
    // physical seam offset so zero corresponds to the configured tracking line.
    estimate.lateral_error = (closest_y - tangent_slope * closest_x) / normal_scale -
                             target_lateral_m_;
    estimate.heading_error = std::atan(tangent_slope);
    const double requested_preview = std::max(0.0, preview_distance_m_);
    const double preview_x = clampValue(requested_preview, min_x, max_x);
    const double preview_u = preview_x / fit_scale;
    const double preview_y = coefficients[0] * preview_u * preview_u +
                             coefficients[1] * preview_u + coefficients[2];
    const double preview_slope = (2.0 * coefficients[0] * preview_u + coefficients[1]) / fit_scale;
    const double second_derivative = 2.0 * coefficients[0] / (fit_scale * fit_scale);
    estimate.preview_lateral_error = preview_y - target_lateral_m_;
    estimate.curvature = second_derivative /
                         std::pow(1.0 + preview_slope * preview_slope, 1.5);
    estimate.points = trajectory_.size();
    estimate.valid = std::isfinite(estimate.lateral_error) && std::isfinite(estimate.heading_error) &&
                     std::isfinite(estimate.preview_lateral_error) && std::isfinite(estimate.curvature) &&
                     std::abs(estimate.heading_error) <= std::max(0.1, max_heading_error_rad_);
    return estimate;
  }

  void onProfile(const crawling_robot_interfaces::msg::LaserProfile& profile) {
    ContourMeasurement measurement;
    const bool valid = extractContour(profile.points, measurement);
    const auto stamp = profile.header.stamp.sec == 0 && profile.header.stamp.nanosec == 0
                           ? now() : rclcpp::Time(profile.header.stamp);
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_profile_time_ = stamp;
    contour_valid_ = valid;
    confidence_ = valid ? measurement.confidence : 0.0;
    if (!valid) {
      geometry_valid_ = false;
      heading_error_rad_ = 0.0;
      preview_lateral_error_m_ = 0.0;
      curvature_1pm_ = 0.0;
      fit_residual_m_ = 0.0;
      preview_integral_m_s_ *= 0.90;
      trajectory_points_ = trajectory_.size();
      target_linear_m_s_ = 0.0;
      target_angular_rad_s_ = 0.0;
      return;
    }

    const auto geometry = valid ? estimateGeometry(stamp, measurement) : GeometryEstimate{};
    const bool geometry_was_valid = geometry_valid_;
    geometry_valid_ = geometry.valid;
    fit_residual_m_ = geometry.residual;
    trajectory_points_ = geometry.points;
    const double fallback_lateral = laser_lateral_offset_m_ + std::sin(laser_yaw_rad_) * measurement.forward +
                                    std::cos(laser_yaw_rad_) * measurement.lateral;
    const double raw_error = geometry.valid ? geometry.lateral_error : fallback_lateral - target_lateral_m_;
    if (geometry.valid && !geometry_was_valid) {
      filtered_derivative_m_s_ = 0.0;
      has_previous_error_ = false;
    }
    if (geometry.valid) {
      filtered_preview_error_m_ += clampValue(preview_filter_alpha_, 0.02, 1.0) *
                                   (geometry.preview_lateral_error - filtered_preview_error_m_);
      filtered_heading_error_ += clampValue(geometry_filter_alpha_, 0.02, 1.0) *
                                 wrapAngle(geometry.heading_error - filtered_heading_error_);
      curvature_1pm_ = clampValue(geometry.curvature, -max_curvature_1pm_, max_curvature_1pm_);
    } else {
      filtered_preview_error_m_ *= 0.98;
      filtered_heading_error_ *= 0.98;
      curvature_1pm_ *= 0.98;
      preview_integral_m_s_ *= 0.90;
    }
    const double dt = has_previous_error_ ? clampValue((stamp - previous_error_time_).seconds(), 0.001, 0.5) : 0.033;
    filtered_error_m_ += clampValue(filter_alpha_, 0.02, 1.0) * (raw_error - filtered_error_m_);
    const double raw_derivative = has_previous_error_ ? (filtered_error_m_ - previous_error_m_) / dt : 0.0;
    filtered_derivative_m_s_ += clampValue(derivative_alpha_, 0.02, 1.0) *
                                (raw_derivative - filtered_derivative_m_s_);
    previous_error_m_ = filtered_error_m_;
    previous_error_time_ = stamp;
    has_previous_error_ = true;

    const double controlled_error = std::abs(filtered_error_m_) < deadband_m_ ? 0.0 : filtered_error_m_;
    const bool integral_allowed = geometry.valid && std::abs(integral_gain_) > 1e-9 &&
                                  std::abs(filtered_heading_error_) <= std::max(0.01, integral_heading_gate_rad_) &&
                                  std::abs(filtered_error_m_) <= std::max(0.005, integral_error_gate_m_);
    const double steering = steering_sign_ == -1 ? -1.0 : 1.0;
    const double heading_term = geometry.valid ? heading_gain_ * filtered_heading_error_ : 0.0;
    const double preview_term = geometry.valid
                                    ? lateral_preview_gain_ *
                                          std::atan(filtered_preview_error_m_ /
                                                    std::max(0.01, preview_distance_m_))
                                    : 0.0;
    const double curvature_term = geometry.valid
                                      ? curvature_feedforward_gain_ * std::max(0.0, target_speed_m_s_) * curvature_1pm_
                                      : 0.0;
    const double feedback_without_integral =
        steering * (curvature_term + heading_term + preview_term + kp_ * controlled_error +
                    kd_ * filtered_derivative_m_s_);
    if (integral_allowed) {
      const double candidate_integral = clampValue(
          preview_integral_m_s_ + filtered_error_m_ * dt,
          -std::max(0.0, integral_limit_m_s_), std::max(0.0, integral_limit_m_s_));
      const double candidate_command = feedback_without_integral +
                                       steering * integral_gain_ * candidate_integral;
      const bool pushes_saturation = std::abs(feedback_without_integral) >= max_angular_z_rad_s_ &&
                                     feedback_without_integral * (candidate_command - feedback_without_integral) > 0.0;
      if (!pushes_saturation) {
        preview_integral_m_s_ = candidate_integral;
      }
    } else {
      preview_integral_m_s_ *= 0.94;
    }
    target_angular_rad_s_ = clampValue(
        feedback_without_integral + steering * integral_gain_ * preview_integral_m_s_,
        -max_angular_z_rad_s_, max_angular_z_rad_s_);
    const double error_ratio = clampValue(std::abs(filtered_error_m_) /
                                              std::max(max_lateral_error_m_, 1e-6),
                                          0.0, 1.0);
    const double speed_scale = clampValue(1.0 - speed_reduction_gain_ * error_ratio,
                                          clampValue(min_speed_scale_, 0.0, 1.0), 1.0);
    target_linear_m_s_ = std::max(0.0, target_speed_m_s_) * speed_scale;
    contour_lateral_m_ = measurement.lateral;
    heading_error_rad_ = geometry.valid ? filtered_heading_error_ : 0.0;
    preview_lateral_error_m_ = geometry.valid ? filtered_preview_error_m_ : 0.0;
  }

  void updateCommand() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!active_) {
      return;
    }
    const auto current_time = now();
    const bool fresh = last_profile_time_.nanoseconds() > 0 &&
                       (current_time - last_profile_time_).nanoseconds() <=
                           static_cast<std::int64_t>(std::max(20, profile_timeout_ms_)) * 1000000LL;
    if (!fresh || !contour_valid_) {
      target_linear_m_s_ = 0.0;
      target_angular_rad_s_ = 0.0;
      if (!fresh) {
        contour_valid_ = false;
        geometry_valid_ = false;
        confidence_ = 0.0;
      }
    }
    const double dt = std::max(0.001, static_cast<double>(control_period_ms_) / 1000.0);
    const double requested_accel = clampValue(
        (target_angular_rad_s_ - current_angular_rad_s_) / dt,
        -std::max(0.01, max_angular_accel_rad_s2_),
        std::max(0.01, max_angular_accel_rad_s2_));
    current_angular_accel_rad_s2_ = rateLimit(
        current_angular_accel_rad_s2_, requested_accel,
        std::max(0.01, max_angular_jerk_rad_s3_) * dt);
    current_angular_rad_s_ = clampValue(
        current_angular_rad_s_ + current_angular_accel_rad_s2_ * dt,
        -max_angular_z_rad_s_, max_angular_z_rad_s_);
    current_linear_m_s_ = rateLimit(current_linear_m_s_, target_linear_m_s_,
                                    std::max(0.01, max_linear_accel_m_s2_) * dt);
    geometry_msgs::msg::Twist command;
    command.linear.x = current_linear_m_s_;
    command.angular.z = current_angular_rad_s_;
    cmd_vel_publisher_->publish(command);
    angular_command_rad_s_ = current_angular_rad_s_;
    angular_accel_rad_s2_ = current_angular_accel_rad_s2_;
    linear_command_m_s_ = current_linear_m_s_;
  }

  static double rateLimit(double current, double target, double step) {
    return current + clampValue(target - current, -step, step);
  }

  void publishZero() {
    geometry_msgs::msg::Twist command;
    cmd_vel_publisher_->publish(command);
  }

  void publishStatus() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    crawling_robot_interfaces::msg::LaserCorrectionStatus status;
    status.header.stamp = now();
    status.header.frame_id = "base_link";
    status.active = active_;
    status.contour_valid = contour_valid_;
    status.geometry_valid = geometry_valid_;
    status.lateral_error_m = filtered_error_m_;
    status.preview_lateral_error_m = preview_lateral_error_m_;
    status.heading_error_rad = heading_error_rad_;
    status.curvature_1pm = curvature_1pm_;
    status.angular_command_rad_s = angular_command_rad_s_;
    status.angular_accel_rad_s2 = angular_accel_rad_s2_;
    status.linear_command_m_s = linear_command_m_s_;
    status.contour_lateral_m = contour_lateral_m_;
    status.confidence = confidence_;
    status.fit_residual_m = fit_residual_m_;
    status.trajectory_points = static_cast<std::uint32_t>(trajectory_points_);
    status_publisher_->publish(status);
  }

  std::string profile_topic_;
  std::string cmd_vel_topic_;
  std::string status_topic_;
  std::string odom_topic_;
  std::string lateral_axis_;
  std::string height_axis_;
  std::string forward_axis_;
  int forward_sign_ = 1;
  int lateral_sign_ = 1;
  int height_sign_ = 1;
  double lateral_min_m_ = -0.2;
  double lateral_max_m_ = 0.2;
  double target_lateral_m_ = 0.0;
  double prominence_threshold_m_ = 0.0015;
  double confidence_scale_m_ = 0.004;
  int min_candidate_points_ = 3;
  double baseline_fraction_ = 0.6;
  int median_window_points_ = 5;
  double contour_half_width_m_ = 0.025;
  double filter_alpha_ = 0.22;
  double derivative_alpha_ = 0.18;
  double kp_ = 3.0;
  double kd_ = 0.35;
  int steering_sign_ = 1;
  double deadband_m_ = 0.0015;
  double max_lateral_error_m_ = 0.08;
  double target_speed_m_s_ = 0.025;
  double min_speed_scale_ = 0.35;
  double speed_reduction_gain_ = 0.8;
  double max_angular_z_rad_s_ = 0.45;
  double max_angular_accel_rad_s2_ = 0.9;
  double max_linear_accel_m_s2_ = 0.05;
  int control_period_ms_ = 20;
  int profile_timeout_ms_ = 180;
  int max_odom_age_ms_ = 120;
  double laser_forward_m_ = 0.0;
  double laser_lateral_offset_m_ = 0.0;
  double laser_yaw_rad_ = 0.0;
  double trajectory_window_m_ = 0.12;
  int max_trajectory_points_ = 80;
  int min_fit_points_ = 6;
  double fit_huber_delta_m_ = 0.006;
  double max_fit_residual_m_ = 0.012;
  double geometry_filter_alpha_ = 0.25;
  double heading_gain_ = 1.6;
  double min_fit_span_m_ = 0.03;
  double max_heading_error_rad_ = 0.785;
  double preview_distance_m_ = 0.06;
  double preview_filter_alpha_ = 0.25;
  double curvature_feedforward_gain_ = 1.0;
  double lateral_preview_gain_ = 2.4;
  double integral_gain_ = 0.0;
  double integral_limit_m_s_ = 0.03;
  double integral_heading_gate_rad_ = 0.0524;
  double integral_error_gate_m_ = 0.03;
  double max_angular_jerk_rad_s3_ = 2.0;
  double max_curvature_1pm_ = 8.0;

  bool active_ = false;
  bool contour_valid_ = false;
  bool geometry_valid_ = false;
  bool has_previous_error_ = false;
  double filtered_error_m_ = 0.0;
  double previous_error_m_ = 0.0;
  double filtered_derivative_m_s_ = 0.0;
  double contour_lateral_m_ = 0.0;
  double confidence_ = 0.0;
  double heading_error_rad_ = 0.0;
  double filtered_heading_error_ = 0.0;
  double filtered_preview_error_m_ = 0.0;
  double preview_lateral_error_m_ = 0.0;
  double curvature_1pm_ = 0.0;
  double preview_integral_m_s_ = 0.0;
  double fit_residual_m_ = 0.0;
  std::size_t trajectory_points_ = 0;
  double target_linear_m_s_ = 0.0;
  double target_angular_rad_s_ = 0.0;
  double current_linear_m_s_ = 0.0;
  double current_angular_rad_s_ = 0.0;
  double current_angular_accel_rad_s2_ = 0.0;
  double linear_command_m_s_ = 0.0;
  double angular_command_rad_s_ = 0.0;
  double angular_accel_rad_s2_ = 0.0;
  rclcpp::Time previous_error_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time last_profile_time_{0, 0, RCL_ROS_TIME};
  std::mutex state_mutex_;

  rclcpp::Subscription<crawling_robot_interfaces::msg::LaserProfile>::SharedPtr profile_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscription_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_publisher_;
  rclcpp::Publisher<crawling_robot_interfaces::msg::LaserCorrectionStatus>::SharedPtr status_publisher_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr enable_service_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset_service_;
  rclcpp::TimerBase::SharedPtr control_timer_;
  rclcpp::TimerBase::SharedPtr status_timer_;
  std::deque<OdomState> odom_history_;
  std::deque<TrajectorySample> trajectory_;
};

}  // namespace crawling_robot_control

namespace {

const char* gFatalNodeName = "laser_path_follower_node";

void fatalTerminateHandler() noexcept {
  std::fprintf(stderr, "[%s] fatal: std::terminate called\n", gFatalNodeName);
  std::fflush(stderr);
  std::abort();
}

void installFatalHandler(const char* node_name) {
  gFatalNodeName = node_name;
  std::set_terminate(&fatalTerminateHandler);
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    installFatalHandler("laser_path_follower_node");
    std::fprintf(stderr, "[laser_path_follower_node] main start\n");
    std::fflush(stderr);
    rclcpp::init(argc, argv);
    crawling_robot_logging::installNodeFileLogger("laser_path_follower_node");
    std::fprintf(stderr, "[laser_path_follower_node] node construction start\n");
    std::fflush(stderr);
    auto node = std::make_shared<crawling_robot_control::LaserPathFollowerNode>();
    std::fprintf(stderr, "[laser_path_follower_node] spin start\n");
    std::fflush(stderr);
    rclcpp::spin(node);
    std::fprintf(stderr, "[laser_path_follower_node] spin end\n");
    std::fflush(stderr);
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[laser_path_follower_node] fatal exception: %s\n", ex.what());
    std::fflush(stderr);
  } catch (...) {
    std::fprintf(stderr, "[laser_path_follower_node] fatal unknown exception\n");
    std::fflush(stderr);
  }
  try {
    rclcpp::shutdown();
  } catch (...) {
  }
  return 1;
}
