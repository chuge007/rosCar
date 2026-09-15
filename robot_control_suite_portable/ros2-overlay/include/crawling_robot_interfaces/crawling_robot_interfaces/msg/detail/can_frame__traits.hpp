// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from crawling_robot_interfaces:msg\CanFrame.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/can_frame.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__TRAITS_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "crawling_robot_interfaces/msg/detail/can_frame__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__traits.hpp"

namespace crawling_robot_interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const CanFrame & msg,
  std::ostream & out)
{
  out << "{";
  // member: header
  {
    out << "header: ";
    to_flow_style_yaml(msg.header, out);
    out << ", ";
  }

  // member: id
  {
    out << "id: ";
    rosidl_generator_traits::value_to_yaml(msg.id, out);
    out << ", ";
  }

  // member: dlc
  {
    out << "dlc: ";
    rosidl_generator_traits::value_to_yaml(msg.dlc, out);
    out << ", ";
  }

  // member: data
  {
    if (msg.data.size() == 0) {
      out << "data: []";
    } else {
      out << "data: [";
      size_t pending_items = msg.data.size();
      for (auto item : msg.data) {
        rosidl_generator_traits::value_to_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const CanFrame & msg,
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

  // member: id
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "id: ";
    rosidl_generator_traits::value_to_yaml(msg.id, out);
    out << "\n";
  }

  // member: dlc
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "dlc: ";
    rosidl_generator_traits::value_to_yaml(msg.dlc, out);
    out << "\n";
  }

  // member: data
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.data.size() == 0) {
      out << "data: []\n";
    } else {
      out << "data:\n";
      for (auto item : msg.data) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "- ";
        rosidl_generator_traits::value_to_yaml(item, out);
        out << "\n";
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const CanFrame & msg, bool use_flow_style = false)
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
  const crawling_robot_interfaces::msg::CanFrame & msg,
  std::ostream & out, size_t indentation = 0)
{
  crawling_robot_interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use crawling_robot_interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const crawling_robot_interfaces::msg::CanFrame & msg)
{
  return crawling_robot_interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<crawling_robot_interfaces::msg::CanFrame>()
{
  return "crawling_robot_interfaces::msg::CanFrame";
}

template<>
inline const char * name<crawling_robot_interfaces::msg::CanFrame>()
{
  return "crawling_robot_interfaces/msg/CanFrame";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::msg::CanFrame>
  : std::integral_constant<bool, has_fixed_size<std_msgs::msg::Header>::value> {};

template<>
struct has_bounded_size<crawling_robot_interfaces::msg::CanFrame>
  : std::integral_constant<bool, has_bounded_size<std_msgs::msg::Header>::value> {};

template<>
struct is_message<crawling_robot_interfaces::msg::CanFrame>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__TRAITS_HPP_
