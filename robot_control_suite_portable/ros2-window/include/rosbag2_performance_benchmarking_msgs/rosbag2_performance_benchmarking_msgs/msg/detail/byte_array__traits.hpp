// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from rosbag2_performance_benchmarking_msgs:msg\ByteArray.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rosbag2_performance_benchmarking_msgs/msg/byte_array.hpp"


#ifndef ROSBAG2_PERFORMANCE_BENCHMARKING_MSGS__MSG__DETAIL__BYTE_ARRAY__TRAITS_HPP_
#define ROSBAG2_PERFORMANCE_BENCHMARKING_MSGS__MSG__DETAIL__BYTE_ARRAY__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "rosbag2_performance_benchmarking_msgs/msg/detail/byte_array__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace rosbag2_performance_benchmarking_msgs
{

namespace msg
{

inline void to_flow_style_yaml(
  const ByteArray & msg,
  std::ostream & out)
{
  out << "{";
  // member: data
  {
    if (msg.data.size() == 0) {
      out << "data: []";
    } else {
      out << "data: [";
      size_t pending_items = msg.data.size();
      for (auto item : msg.data) {
        rosidl_generator_traits::character_value_to_yaml(item, out);
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
  const ByteArray & msg,
  std::ostream & out, size_t indentation = 0)
{
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
        rosidl_generator_traits::character_value_to_yaml(item, out);
        out << "\n";
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const ByteArray & msg, bool use_flow_style = false)
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

}  // namespace rosbag2_performance_benchmarking_msgs

namespace rosidl_generator_traits
{

[[deprecated("use rosbag2_performance_benchmarking_msgs::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const rosbag2_performance_benchmarking_msgs::msg::ByteArray & msg,
  std::ostream & out, size_t indentation = 0)
{
  rosbag2_performance_benchmarking_msgs::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use rosbag2_performance_benchmarking_msgs::msg::to_yaml() instead")]]
inline std::string to_yaml(const rosbag2_performance_benchmarking_msgs::msg::ByteArray & msg)
{
  return rosbag2_performance_benchmarking_msgs::msg::to_yaml(msg);
}

template<>
inline const char * data_type<rosbag2_performance_benchmarking_msgs::msg::ByteArray>()
{
  return "rosbag2_performance_benchmarking_msgs::msg::ByteArray";
}

template<>
inline const char * name<rosbag2_performance_benchmarking_msgs::msg::ByteArray>()
{
  return "rosbag2_performance_benchmarking_msgs/msg/ByteArray";
}

template<>
struct has_fixed_size<rosbag2_performance_benchmarking_msgs::msg::ByteArray>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<rosbag2_performance_benchmarking_msgs::msg::ByteArray>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<rosbag2_performance_benchmarking_msgs::msg::ByteArray>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // ROSBAG2_PERFORMANCE_BENCHMARKING_MSGS__MSG__DETAIL__BYTE_ARRAY__TRAITS_HPP_
