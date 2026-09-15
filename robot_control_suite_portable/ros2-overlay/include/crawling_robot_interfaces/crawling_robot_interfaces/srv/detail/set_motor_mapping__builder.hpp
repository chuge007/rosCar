// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from crawling_robot_interfaces:srv\SetMotorMapping.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_motor_mapping.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__BUILDER_HPP_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "crawling_robot_interfaces/srv/detail/set_motor_mapping__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetMotorMapping_Request_right_motor_sign
{
public:
  explicit Init_SetMotorMapping_Request_right_motor_sign(::crawling_robot_interfaces::srv::SetMotorMapping_Request & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetMotorMapping_Request right_motor_sign(::crawling_robot_interfaces::srv::SetMotorMapping_Request::_right_motor_sign_type arg)
  {
    msg_.right_motor_sign = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Request msg_;
};

class Init_SetMotorMapping_Request_left_motor_sign
{
public:
  explicit Init_SetMotorMapping_Request_left_motor_sign(::crawling_robot_interfaces::srv::SetMotorMapping_Request & msg)
  : msg_(msg)
  {}
  Init_SetMotorMapping_Request_right_motor_sign left_motor_sign(::crawling_robot_interfaces::srv::SetMotorMapping_Request::_left_motor_sign_type arg)
  {
    msg_.left_motor_sign = std::move(arg);
    return Init_SetMotorMapping_Request_right_motor_sign(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Request msg_;
};

class Init_SetMotorMapping_Request_right_motor_id
{
public:
  explicit Init_SetMotorMapping_Request_right_motor_id(::crawling_robot_interfaces::srv::SetMotorMapping_Request & msg)
  : msg_(msg)
  {}
  Init_SetMotorMapping_Request_left_motor_sign right_motor_id(::crawling_robot_interfaces::srv::SetMotorMapping_Request::_right_motor_id_type arg)
  {
    msg_.right_motor_id = std::move(arg);
    return Init_SetMotorMapping_Request_left_motor_sign(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Request msg_;
};

class Init_SetMotorMapping_Request_left_motor_id
{
public:
  Init_SetMotorMapping_Request_left_motor_id()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetMotorMapping_Request_right_motor_id left_motor_id(::crawling_robot_interfaces::srv::SetMotorMapping_Request::_left_motor_id_type arg)
  {
    msg_.left_motor_id = std::move(arg);
    return Init_SetMotorMapping_Request_right_motor_id(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetMotorMapping_Request>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetMotorMapping_Request_left_motor_id();
}

}  // namespace crawling_robot_interfaces


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetMotorMapping_Response_message
{
public:
  explicit Init_SetMotorMapping_Response_message(::crawling_robot_interfaces::srv::SetMotorMapping_Response & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetMotorMapping_Response message(::crawling_robot_interfaces::srv::SetMotorMapping_Response::_message_type arg)
  {
    msg_.message = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Response msg_;
};

class Init_SetMotorMapping_Response_success
{
public:
  Init_SetMotorMapping_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetMotorMapping_Response_message success(::crawling_robot_interfaces::srv::SetMotorMapping_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_SetMotorMapping_Response_message(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetMotorMapping_Response>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetMotorMapping_Response_success();
}

}  // namespace crawling_robot_interfaces


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetMotorMapping_Event_response
{
public:
  explicit Init_SetMotorMapping_Event_response(::crawling_robot_interfaces::srv::SetMotorMapping_Event & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetMotorMapping_Event response(::crawling_robot_interfaces::srv::SetMotorMapping_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Event msg_;
};

class Init_SetMotorMapping_Event_request
{
public:
  explicit Init_SetMotorMapping_Event_request(::crawling_robot_interfaces::srv::SetMotorMapping_Event & msg)
  : msg_(msg)
  {}
  Init_SetMotorMapping_Event_response request(::crawling_robot_interfaces::srv::SetMotorMapping_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_SetMotorMapping_Event_response(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Event msg_;
};

class Init_SetMotorMapping_Event_info
{
public:
  Init_SetMotorMapping_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetMotorMapping_Event_request info(::crawling_robot_interfaces::srv::SetMotorMapping_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_SetMotorMapping_Event_request(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetMotorMapping_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetMotorMapping_Event>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetMotorMapping_Event_info();
}

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__BUILDER_HPP_
