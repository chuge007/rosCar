// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from crawling_robot_interfaces:msg\LaserCorrectionStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_correction_status.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__TRAITS_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "crawling_robot_interfaces/msg/detail/laser_correction_status__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"

namespace crawling_robot_interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const LaserCorrectionStatus & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: active
  {
    out << "active: ";
    rosidl_generator_traits::value_to_yaml(msg.active, out);
    out << ", ";
  }

  // member: contour_valid
  {
    out << "contour_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.contour_valid, out);
    out << ", ";
  }

  // member: geometry_valid
  {
    out << "geometry_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.geometry_valid, out);
    out << ", ";
  }

  // member: lateral_error_m
  {
    out << "lateral_error_m: ";
    rosidl_generator_traits::value_to_yaml(msg.lateral_error_m, out);
    out << ", ";
  }

  // member: preview_lateral_error_m
  {
    out << "preview_lateral_error_m: ";
    rosidl_generator_traits::value_to_yaml(msg.preview_lateral_error_m, out);
    out << ", ";
  }

  // member: heading_error_rad
  {
    out << "heading_error_rad: ";
    rosidl_generator_traits::value_to_yaml(msg.heading_error_rad, out);
    out << ", ";
  }

  // member: curvature_1pm
  {
    out << "curvature_1pm: ";
    rosidl_generator_traits::value_to_yaml(msg.curvature_1pm, out);
    out << ", ";
  }

  // member: angular_command_rad_s
  {
    out << "angular_command_rad_s: ";
    rosidl_generator_traits::value_to_yaml(msg.angular_command_rad_s, out);
    out << ", ";
  }

  // member: angular_accel_rad_s2
  {
    out << "angular_accel_rad_s2: ";
    rosidl_generator_traits::value_to_yaml(msg.angular_accel_rad_s2, out);
    out << ", ";
  }

  // member: linear_command_m_s
  {
    out << "linear_command_m_s: ";
    rosidl_generator_traits::value_to_yaml(msg.linear_command_m_s, out);
    out << ", ";
  }

  // member: contour_lateral_m
  {
    out << "contour_lateral_m: ";
    rosidl_generator_traits::value_to_yaml(msg.contour_lateral_m, out);
    out << ", ";
  }

  // member: confidence
  {
    out << "confidence: ";
    rosidl_generator_traits::value_to_yaml(msg.confidence, out);
    out << ", ";
  }

  // member: fit_residual_m
  {
    out << "fit_residual_m: ";
    rosidl_generator_traits::value_to_yaml(msg.fit_residual_m, out);
    out << ", ";
  }

  // member: trajectory_points
  {
    out << "trajectory_points: ";
    rosidl_generator_traits::value_to_yaml(msg.trajectory_points, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const LaserCorrectionStatus & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: header
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "header:\n";
    to_block_style_yaml(msg.header, out, indentation + 2);
  }

  // member: active
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "active: ";
    rosidl_generator_traits::value_to_yaml(msg.active, out);
    out << "\n";
  }

  // member: contour_valid
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "contour_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.contour_valid, out);
    out << "\n";
  }

  // member: geometry_valid
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "geometry_valid: ";
    rosidl_generator_traits::value_to_yaml(msg.geometry_valid, out);
    out << "\n";
  }

  // member: lateral_error_m
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "lateral_error_m: ";
    rosidl_generator_traits::value_to_yaml(msg.lateral_error_m, out);
    out << "\n";
  }

  // member: preview_lateral_error_m
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "preview_lateral_error_m: ";
    rosidl_generator_traits::value_to_yaml(msg.preview_lateral_error_m, out);
    out << "\n";
  }

  // member: heading_error_rad
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "heading_error_rad: ";
    rosidl_generator_traits::value_to_yaml(msg.heading_error_rad, out);
    out << "\n";
  }

  // member: curvature_1pm
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "curvature_1pm: ";
    rosidl_generator_traits::value_to_yaml(msg.curvature_1pm, out);
    out << "\n";
  }

  // member: angular_command_rad_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "angular_command_rad_s: ";
    rosidl_generator_traits::value_to_yaml(msg.angular_command_rad_s, out);
    out << "\n";
  }

  // member: angular_accel_rad_s2
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "angular_accel_rad_s2: ";
    rosidl_generator_traits::value_to_yaml(msg.angular_accel_rad_s2, out);
    out << "\n";
  }

  // member: linear_command_m_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "linear_command_m_s: ";
    rosidl_generator_traits::value_to_yaml(msg.linear_command_m_s, out);
    out << "\n";
  }

  // member: contour_lateral_m
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "contour_lateral_m: ";
    rosidl_generator_traits::value_to_yaml(msg.contour_lateral_m, out);
    out << "\n";
  }

  // member: confidence
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "confidence: ";
    rosidl_generator_traits::value_to_yaml(msg.confidence, out);
    out << "\n";
  }

  // member: fit_residual_m
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "fit_residual_m: ";
    rosidl_generator_traits::value_to_yaml(msg.fit_residual_m, out);
    out << "\n";
  }

  // member: trajectory_points
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "trajectory_points: ";
    rosidl_generator_traits::value_to_yaml(msg.trajectory_points, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const LaserCorrectionStatus & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace crawling_robot_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use crawling_robot_interfaces::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const crawling_robot_interfaces::msg::LaserCorrectionStatus & msg,
  std::ostream & out, size_t indentation = 0)
{
  crawling_robot_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use crawling_robot_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
{
  return crawling_robot_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<crawling_robot_interfaces::msg::LaserCorrectionStatus>()
{
  return "crawling_robot_interfaces::msg::LaserCorrectionStatus";
}

template<>
inline const char * name<crawling_robot_interfaces::msg::LaserCorrectionStatus>()
{
  return "crawling_robot_interfaces/msg/LaserCorrectionStatus";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::msg::LaserCorrectionStatus>
  : std::integral_constant<bool, has_fixed_size<std_msgs::msg::Header>::value> {};

template<>
struct has_bounded_size<crawling_robot_interfaces::msg::LaserCorrectionStatus>
  : std::integral_constant<bool, has_bounded_size<std_msgs::msg::Header>::value> {};

template<>
struct is_message<crawling_robot_interfaces::msg::LaserCorrectionStatus>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__TRAITS_HPP_
