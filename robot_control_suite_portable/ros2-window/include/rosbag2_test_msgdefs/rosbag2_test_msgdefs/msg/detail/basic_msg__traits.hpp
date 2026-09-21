// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from rosbag2_test_msgdefs:msg\BasicMsg.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rosbag2_test_msgdefs/msg/basic_msg.hpp"


#ifndef ROSBAG2_TEST_MSGDEFS__MSG__DETAIL__BASIC_MSG__TRAITS_HPP_
#define ROSBAG2_TEST_MSGDEFS__MSG__DETAIL__BASIC_MSG__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "rosbag2_test_msgdefs/msg/detail/basic_msg__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace rosbag2_test_msgdefs
{

namespace msg
{

inline void to_flow_style_yaml(
  const BasicMsg & msg,
  std::ostream & out)
{
  out << "{";
  // member: c
  {
    out << "c: ";
    rosidl_generator_traits::value_to_yaml(msg.c, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const BasicMsg & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: c
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "c: ";
    rosidl_generator_traits::value_to_yaml(msg.c, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const BasicMsg & msg, bool use_flow_style = false)
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

}  // namespace rosbag2_test_msgdefs

namespace rosidl_generator_traits
{

[[deprecated("use rosbag2_test_msgdefs::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const rosbag2_test_msgdefs::msg::BasicMsg & msg,
  std::ostream & out, size_t indentation = 0)
{
  rosbag2_test_msgdefs::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use rosbag2_test_msgdefs::msg::to_yaml() instead")]]
inline std::string to_yaml(const rosbag2_test_msgdefs::msg::BasicMsg & msg)
{
  return rosbag2_test_msgdefs::msg::to_yaml(msg);
}

template<>
inline const char * data_type<rosbag2_test_msgdefs::msg::BasicMsg>()
{
  return "rosbag2_test_msgdefs::msg::BasicMsg";
}

template<>
inline const char * name<rosbag2_test_msgdefs::msg::BasicMsg>()
{
  return "rosbag2_test_msgdefs/msg/BasicMsg";
}

template<>
struct has_fixed_size<rosbag2_test_msgdefs::msg::BasicMsg>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<rosbag2_test_msgdefs::msg::BasicMsg>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<rosbag2_test_msgdefs::msg::BasicMsg>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // ROSBAG2_TEST_MSGDEFS__MSG__DETAIL__BASIC_MSG__TRAITS_HPP_
