// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from crawling_robot_interfaces:srv\SetDriveLimits.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_drive_limits.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__BUILDER_HPP_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "crawling_robot_interfaces/srv/detail/set_drive_limits__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetDriveLimits_Request_minimum_inner_wheel_ratio
{
public:
  explicit Init_SetDriveLimits_Request_minimum_inner_wheel_ratio(::crawling_robot_interfaces::srv::SetDriveLimits_Request & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request minimum_inner_wheel_ratio(::crawling_robot_interfaces::srv::SetDriveLimits_Request::_minimum_inner_wheel_ratio_type arg)
  {
    msg_.minimum_inner_wheel_ratio = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request msg_;
};

class Init_SetDriveLimits_Request_max_wheel_speed_m_s
{
public:
  explicit Init_SetDriveLimits_Request_max_wheel_speed_m_s(::crawling_robot_interfaces::srv::SetDriveLimits_Request & msg)
  : msg_(msg)
  {}
  Init_SetDriveLimits_Request_minimum_inner_wheel_ratio max_wheel_speed_m_s(::crawling_robot_interfaces::srv::SetDriveLimits_Request::_max_wheel_speed_m_s_type arg)
  {
    msg_.max_wheel_speed_m_s = std::move(arg);
    return Init_SetDriveLimits_Request_minimum_inner_wheel_ratio(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request msg_;
};

class Init_SetDriveLimits_Request_max_angular_accel_rad_s2
{
public:
  explicit Init_SetDriveLimits_Request_max_angular_accel_rad_s2(::crawling_robot_interfaces::srv::SetDriveLimits_Request & msg)
  : msg_(msg)
  {}
  Init_SetDriveLimits_Request_max_wheel_speed_m_s max_angular_accel_rad_s2(::crawling_robot_interfaces::srv::SetDriveLimits_Request::_max_angular_accel_rad_s2_type arg)
  {
    msg_.max_angular_accel_rad_s2 = std::move(arg);
    return Init_SetDriveLimits_Request_max_wheel_speed_m_s(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request msg_;
};

class Init_SetDriveLimits_Request_max_linear_accel_m_s2
{
public:
  explicit Init_SetDriveLimits_Request_max_linear_accel_m_s2(::crawling_robot_interfaces::srv::SetDriveLimits_Request & msg)
  : msg_(msg)
  {}
  Init_SetDriveLimits_Request_max_angular_accel_rad_s2 max_linear_accel_m_s2(::crawling_robot_interfaces::srv::SetDriveLimits_Request::_max_linear_accel_m_s2_type arg)
  {
    msg_.max_linear_accel_m_s2 = std::move(arg);
    return Init_SetDriveLimits_Request_max_angular_accel_rad_s2(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request msg_;
};

class Init_SetDriveLimits_Request_max_angular_speed_rad_s
{
public:
  explicit Init_SetDriveLimits_Request_max_angular_speed_rad_s(::crawling_robot_interfaces::srv::SetDriveLimits_Request & msg)
  : msg_(msg)
  {}
  Init_SetDriveLimits_Request_max_linear_accel_m_s2 max_angular_speed_rad_s(::crawling_robot_interfaces::srv::SetDriveLimits_Request::_max_angular_speed_rad_s_type arg)
  {
    msg_.max_angular_speed_rad_s = std::move(arg);
    return Init_SetDriveLimits_Request_max_linear_accel_m_s2(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request msg_;
};

class Init_SetDriveLimits_Request_max_linear_speed_m_s
{
public:
  Init_SetDriveLimits_Request_max_linear_speed_m_s()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetDriveLimits_Request_max_angular_speed_rad_s max_linear_speed_m_s(::crawling_robot_interfaces::srv::SetDriveLimits_Request::_max_linear_speed_m_s_type arg)
  {
    msg_.max_linear_speed_m_s = std::move(arg);
    return Init_SetDriveLimits_Request_max_angular_speed_rad_s(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetDriveLimits_Request>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetDriveLimits_Request_max_linear_speed_m_s();
}

}  // namespace crawling_robot_interfaces


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetDriveLimits_Response_message
{
public:
  explicit Init_SetDriveLimits_Response_message(::crawling_robot_interfaces::srv::SetDriveLimits_Response & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetDriveLimits_Response message(::crawling_robot_interfaces::srv::SetDriveLimits_Response::_message_type arg)
  {
    msg_.message = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Response msg_;
};

class Init_SetDriveLimits_Response_success
{
public:
  Init_SetDriveLimits_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetDriveLimits_Response_message success(::crawling_robot_interfaces::srv::SetDriveLimits_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_SetDriveLimits_Response_message(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetDriveLimits_Response>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetDriveLimits_Response_success();
}

}  // namespace crawling_robot_interfaces


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetDriveLimits_Event_response
{
public:
  explicit Init_SetDriveLimits_Event_response(::crawling_robot_interfaces::srv::SetDriveLimits_Event & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetDriveLimits_Event response(::crawling_robot_interfaces::srv::SetDriveLimits_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Event msg_;
};

class Init_SetDriveLimits_Event_request
{
public:
  explicit Init_SetDriveLimits_Event_request(::crawling_robot_interfaces::srv::SetDriveLimits_Event & msg)
  : msg_(msg)
  {}
  Init_SetDriveLimits_Event_response request(::crawling_robot_interfaces::srv::SetDriveLimits_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_SetDriveLimits_Event_response(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Event msg_;
};

class Init_SetDriveLimits_Event_info
{
public:
  Init_SetDriveLimits_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetDriveLimits_Event_request info(::crawling_robot_interfaces::srv::SetDriveLimits_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_SetDriveLimits_Event_request(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetDriveLimits_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetDriveLimits_Event>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetDriveLimits_Event_info();
}

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__BUILDER_HPP_
