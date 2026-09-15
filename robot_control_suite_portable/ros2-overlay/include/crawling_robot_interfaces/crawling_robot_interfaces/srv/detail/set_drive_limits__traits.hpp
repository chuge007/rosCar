// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from crawling_robot_interfaces:srv\SetDriveLimits.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_drive_limits.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__TRAITS_HPP_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "crawling_robot_interfaces/srv/detail/set_drive_limits__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace crawling_robot_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetDriveLimits_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: max_linear_speed_m_s
  {
    out << "max_linear_speed_m_s: ";
    rosidl_generator_traits::value_to_yaml(msg.max_linear_speed_m_s, out);
    out << ", ";
  }

  // member: max_angular_speed_rad_s
  {
    out << "max_angular_speed_rad_s: ";
    rosidl_generator_traits::value_to_yaml(msg.max_angular_speed_rad_s, out);
    out << ", ";
  }

  // member: max_linear_accel_m_s2
  {
    out << "max_linear_accel_m_s2: ";
    rosidl_generator_traits::value_to_yaml(msg.max_linear_accel_m_s2, out);
    out << ", ";
  }

  // member: max_angular_accel_rad_s2
  {
    out << "max_angular_accel_rad_s2: ";
    rosidl_generator_traits::value_to_yaml(msg.max_angular_accel_rad_s2, out);
    out << ", ";
  }

  // member: max_wheel_speed_m_s
  {
    out << "max_wheel_speed_m_s: ";
    rosidl_generator_traits::value_to_yaml(msg.max_wheel_speed_m_s, out);
    out << ", ";
  }

  // member: minimum_inner_wheel_ratio
  {
    out << "minimum_inner_wheel_ratio: ";
    rosidl_generator_traits::value_to_yaml(msg.minimum_inner_wheel_ratio, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const SetDriveLimits_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: max_linear_speed_m_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "max_linear_speed_m_s: ";
    rosidl_generator_traits::value_to_yaml(msg.max_linear_speed_m_s, out);
    out << "\n";
  }

  // member: max_angular_speed_rad_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "max_angular_speed_rad_s: ";
    rosidl_generator_traits::value_to_yaml(msg.max_angular_speed_rad_s, out);
    out << "\n";
  }

  // member: max_linear_accel_m_s2
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "max_linear_accel_m_s2: ";
    rosidl_generator_traits::value_to_yaml(msg.max_linear_accel_m_s2, out);
    out << "\n";
  }

  // member: max_angular_accel_rad_s2
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "max_angular_accel_rad_s2: ";
    rosidl_generator_traits::value_to_yaml(msg.max_angular_accel_rad_s2, out);
    out << "\n";
  }

  // member: max_wheel_speed_m_s
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "max_wheel_speed_m_s: ";
    rosidl_generator_traits::value_to_yaml(msg.max_wheel_speed_m_s, out);
    out << "\n";
  }

  // member: minimum_inner_wheel_ratio
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "minimum_inner_wheel_ratio: ";
    rosidl_generator_traits::value_to_yaml(msg.minimum_inner_wheel_ratio, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetDriveLimits_Request & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace crawling_robot_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use crawling_robot_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const crawling_robot_interfaces::srv::SetDriveLimits_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  crawling_robot_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use crawling_robot_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const crawling_robot_interfaces::srv::SetDriveLimits_Request & msg)
{
  return crawling_robot_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<crawling_robot_interfaces::srv::SetDriveLimits_Request>()
{
  return "crawling_robot_interfaces::srv::SetDriveLimits_Request";
}

template<>
inline const char * name<crawling_robot_interfaces::srv::SetDriveLimits_Request>()
{
  return "crawling_robot_interfaces/srv/SetDriveLimits_Request";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::srv::SetDriveLimits_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<crawling_robot_interfaces::srv::SetDriveLimits_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace crawling_robot_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetDriveLimits_Response & msg,
  std::ostream & out)
{
  out << "{";
  // member: success
  {
    out << "success: ";
    rosidl_generator_traits::value_to_yaml(msg.success, out);
    out << ", ";
  }

  // member: message
  {
    out << "message: ";
    rosidl_generator_traits::value_to_yaml(msg.message, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const SetDriveLimits_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: success
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "success: ";
    rosidl_generator_traits::value_to_yaml(msg.success, out);
    out << "\n";
  }

  // member: message
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "message: ";
    rosidl_generator_traits::value_to_yaml(msg.message, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetDriveLimits_Response & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace crawling_robot_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use crawling_robot_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const crawling_robot_interfaces::srv::SetDriveLimits_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  crawling_robot_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use crawling_robot_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const crawling_robot_interfaces::srv::SetDriveLimits_Response & msg)
{
  return crawling_robot_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<crawling_robot_interfaces::srv::SetDriveLimits_Response>()
{
  return "crawling_robot_interfaces::srv::SetDriveLimits_Response";
}

template<>
inline const char * name<crawling_robot_interfaces::srv::SetDriveLimits_Response>()
{
  return "crawling_robot_interfaces/srv/SetDriveLimits_Response";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::srv::SetDriveLimits_Response>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Response>
  : std::integral_constant<bool, false> {};

template<>
struct is_message<crawling_robot_interfaces::srv::SetDriveLimits_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__traits.hpp"

namespace crawling_robot_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const SetDriveLimits_Event & msg,
  std::ostream & out)
{
  out << "{";
  // member: info
  {
    out << "info: ";
    to_flow_style_yaml(msg.info, out);
    out << ", ";
  }

  // member: request
  {
    if (msg.request.size() == 0) {
      out << "request: []";
    } else {
      out << "request: [";
      size_t pending_items = msg.request.size();
      for (auto item : msg.request) {
        to_flow_style_yaml(item, out);
        if (--pending_items > 0) {
          out << ", ";
        }
      }
      out << "]";
    }
    out << ", ";
  }

  // member: response
  {
    if (msg.response.size() == 0) {
      out << "response: []";
    } else {
      out << "response: [";
      size_t pending_items = msg.response.size();
      for (auto item : msg.response) {
        to_flow_style_yaml(item, out);
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
  const SetDriveLimits_Event & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: info
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "info:\n";
    to_block_style_yaml(msg.info, out, indentation + 2);
  }

  // member: request
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.request.size() == 0) {
      out << "request: []\n";
    } else {
      out << "request:\n";
      for (auto item : msg.request) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "-\n";
        to_block_style_yaml(item, out, indentation + 2);
      }
    }
  }

  // member: response
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    if (msg.response.size() == 0) {
      out << "response: []\n";
    } else {
      out << "response:\n";
      for (auto item : msg.response) {
        if (indentation > 0) {
          out << std::string(indentation, ' ');
        }
        out << "-\n";
        to_block_style_yaml(item, out, indentation + 2);
      }
    }
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const SetDriveLimits_Event & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace crawling_robot_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use crawling_robot_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const crawling_robot_interfaces::srv::SetDriveLimits_Event & msg,
  std::ostream & out, size_t indentation = 0)
{
  crawling_robot_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use crawling_robot_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const crawling_robot_interfaces::srv::SetDriveLimits_Event & msg)
{
  return crawling_robot_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<crawling_robot_interfaces::srv::SetDriveLimits_Event>()
{
  return "crawling_robot_interfaces::srv::SetDriveLimits_Event";
}

template<>
inline const char * name<crawling_robot_interfaces::srv::SetDriveLimits_Event>()
{
  return "crawling_robot_interfaces/srv/SetDriveLimits_Event";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::srv::SetDriveLimits_Event>
  : std::integral_constant<bool, false> {};

template<>
struct has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Event>
  : std::integral_constant<bool, has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Request>::value && has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Response>::value && has_bounded_size<service_msgs::msg::ServiceEventInfo>::value> {};

template<>
struct is_message<crawling_robot_interfaces::srv::SetDriveLimits_Event>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<crawling_robot_interfaces::srv::SetDriveLimits>()
{
  return "crawling_robot_interfaces::srv::SetDriveLimits";
}

template<>
inline const char * name<crawling_robot_interfaces::srv::SetDriveLimits>()
{
  return "crawling_robot_interfaces/srv/SetDriveLimits";
}

template<>
struct has_fixed_size<crawling_robot_interfaces::srv::SetDriveLimits>
  : std::integral_constant<
    bool,
    has_fixed_size<crawling_robot_interfaces::srv::SetDriveLimits_Request>::value &&
    has_fixed_size<crawling_robot_interfaces::srv::SetDriveLimits_Response>::value
  >
{
};

template<>
struct has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits>
  : std::integral_constant<
    bool,
    has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Request>::value &&
    has_bounded_size<crawling_robot_interfaces::srv::SetDriveLimits_Response>::value
  >
{
};

template<>
struct is_service<crawling_robot_interfaces::srv::SetDriveLimits>
  : std::true_type
{
};

template<>
struct is_service_request<crawling_robot_interfaces::srv::SetDriveLimits_Request>
  : std::true_type
{
};

template<>
struct is_service_response<crawling_robot_interfaces::srv::SetDriveLimits_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__TRAITS_HPP_
