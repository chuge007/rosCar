// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from crawling_robot_interfaces:msg\LaserProfile.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_profile.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__TRAITS_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "crawling_robot_interfaces/msg/detail/laser_profile__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"
// Member 'points'
#include "sensor_msgs/msg/detail/point_cloud2__traits.hpp"

namespace crawling_robot_interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const LaserProfile & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: points
  {
    out << "points: ";
    to_flow_style_yaml(msg.points, out);
    out << ", ";
  }

  // member: encoder_ticks
  {
    out << "encoder_ticks: ";
    rosidl_generator_traits::value_to_yaml(msg.encoder_ticks, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const LaserProfile & msg,
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

  // member: points
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "points:\n";
    to_block_style_yaml(msg.points, out, indentation + 2);
  }

  // member: encoder_ticks
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "encoder_ticks: ";
    rosidl_generator_traits::value_to_yaml(msg.encoder_ticks, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const LaserProfile & msg, bool use_flow_style = false)
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
  const crawling_robot_interfaces::msg::LaserProfile & msg,
  std::ostream & out, size_t indentation = 0)
{
  crawling_robot_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use crawling_robot_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const crawling_robot_interfaces::msg::LaserProfile & msg)
{
  return crawling_robot_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<crawling_robot_interfaces::msg::LaserProfile>()
{
  return "crawling_robot_interfaces::msg::LaserProfile";
}

template<>
inline const char * name<crawling_robot_interfaces::msg::LaserProfile>()
{
  return "crawling_robot_interfaces/msg/LaserProfile";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::msg::LaserProfile>
  : std::integral_constant<bool, has_fixed_size<sensor_msgs::msg::PointCloud2>::value && has_fixed_size<std_msgs::msg::Header>::value> {};

template<>
struct has_bounded_size<crawling_robot_interfaces::msg::LaserProfile>
  : std::integral_constant<bool, has_bounded_size<sensor_msgs::msg::PointCloud2>::value && has_bounded_size<std_msgs::msg::Header>::value> {};

template<>
struct is_message<crawling_robot_interfaces::msg::LaserProfile>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__TRAITS_HPP_
