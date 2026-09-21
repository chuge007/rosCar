// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from rosbag2_test_msgdefs:msg\BasicIdl.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "rosbag2_test_msgdefs/msg/basic_idl.hpp"


#ifndef ROSBAG2_TEST_MSGDEFS__MSG__DETAIL__BASIC_IDL__TRAITS_HPP_
#define ROSBAG2_TEST_MSGDEFS__MSG__DETAIL__BASIC_IDL__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "rosbag2_test_msgdefs/msg/detail/basic_idl__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace rosbag2_test_msgdefs
{

namespace msg
{

inline void to_flow_style_yaml(
  const BasicIdl & msg,
  std::ostream & out)
{
  out << "{";
  // member: x
  {
    out << "x: ";
    rosidl_generator_traits::value_to_yaml(msg.x, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const BasicIdl & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: x
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "x: ";
    rosidl_generator_traits::value_to_yaml(msg.x, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const BasicIdl & msg, bool use_flow_style = false)
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
  const rosbag2_test_msgdefs::msg::BasicIdl & msg,
  std::ostream & out, size_t indentation = 0)
{
  rosbag2_test_msgdefs::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use rosbag2_test_msgdefs::msg::to_yaml() instead")]]
inline std::string to_yaml(const rosbag2_test_msgdefs::msg::BasicIdl & msg)
{
  return rosbag2_test_msgdefs::msg::to_yaml(msg);
}

template<>
inline const char * data_type<rosbag2_test_msgdefs::msg::BasicIdl>()
{
  return "rosbag2_test_msgdefs::msg::BasicIdl";
}

template<>
inline const char * name<rosbag2_test_msgdefs::msg::BasicIdl>()
{
  return "rosbag2_test_msgdefs/msg/BasicIdl";
}

template<>
struct has_fixed_size<rosbag2_test_msgdefs::msg::BasicIdl>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<rosbag2_test_msgdefs::msg::BasicIdl>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<rosbag2_test_msgdefs::msg::BasicIdl>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // ROSBAG2_TEST_MSGDEFS__MSG__DETAIL__BASIC_IDL__TRAITS_HPP_
