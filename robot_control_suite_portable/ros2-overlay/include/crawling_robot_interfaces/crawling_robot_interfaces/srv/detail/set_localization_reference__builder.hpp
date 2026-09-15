// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from crawling_robot_interfaces:srv\SetLocalizationReference.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_localization_reference.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__BUILDER_HPP_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "crawling_robot_interfaces/srv/detail/set_localization_reference__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetLocalizationReference_Request_heading_reference_rad
{
public:
  explicit Init_SetLocalizationReference_Request_heading_reference_rad(::crawling_robot_interfaces::srv::SetLocalizationReference_Request & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Request heading_reference_rad(::crawling_robot_interfaces::srv::SetLocalizationReference_Request::_heading_reference_rad_type arg)
  {
    msg_.heading_reference_rad = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Request msg_;
};

class Init_SetLocalizationReference_Request_contour_lateral_m
{
public:
  Init_SetLocalizationReference_Request_contour_lateral_m()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetLocalizationReference_Request_heading_reference_rad contour_lateral_m(::crawling_robot_interfaces::srv::SetLocalizationReference_Request::_contour_lateral_m_type arg)
  {
    msg_.contour_lateral_m = std::move(arg);
    return Init_SetLocalizationReference_Request_heading_reference_rad(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetLocalizationReference_Request>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetLocalizationReference_Request_contour_lateral_m();
}

}  // namespace crawling_robot_interfaces


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetLocalizationReference_Response_message
{
public:
  explicit Init_SetLocalizationReference_Response_message(::crawling_robot_interfaces::srv::SetLocalizationReference_Response & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Response message(::crawling_robot_interfaces::srv::SetLocalizationReference_Response::_message_type arg)
  {
    msg_.message = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Response msg_;
};

class Init_SetLocalizationReference_Response_success
{
public:
  Init_SetLocalizationReference_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetLocalizationReference_Response_message success(::crawling_robot_interfaces::srv::SetLocalizationReference_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return Init_SetLocalizationReference_Response_message(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetLocalizationReference_Response>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetLocalizationReference_Response_success();
}

}  // namespace crawling_robot_interfaces


namespace crawling_robot_interfaces
{

namespace srv
{

namespace builder
{

class Init_SetLocalizationReference_Event_response
{
public:
  explicit Init_SetLocalizationReference_Event_response(::crawling_robot_interfaces::srv::SetLocalizationReference_Event & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Event response(::crawling_robot_interfaces::srv::SetLocalizationReference_Event::_response_type arg)
  {
    msg_.response = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Event msg_;
};

class Init_SetLocalizationReference_Event_request
{
public:
  explicit Init_SetLocalizationReference_Event_request(::crawling_robot_interfaces::srv::SetLocalizationReference_Event & msg)
  : msg_(msg)
  {}
  Init_SetLocalizationReference_Event_response request(::crawling_robot_interfaces::srv::SetLocalizationReference_Event::_request_type arg)
  {
    msg_.request = std::move(arg);
    return Init_SetLocalizationReference_Event_response(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Event msg_;
};

class Init_SetLocalizationReference_Event_info
{
public:
  Init_SetLocalizationReference_Event_info()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_SetLocalizationReference_Event_request info(::crawling_robot_interfaces::srv::SetLocalizationReference_Event::_info_type arg)
  {
    msg_.info = std::move(arg);
    return Init_SetLocalizationReference_Event_request(msg_);
  }

private:
  ::crawling_robot_interfaces::srv::SetLocalizationReference_Event msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::srv::SetLocalizationReference_Event>()
{
  return crawling_robot_interfaces::srv::builder::Init_SetLocalizationReference_Event_info();
}

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__BUILDER_HPP_
