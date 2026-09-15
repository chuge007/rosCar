// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from crawling_robot_interfaces:msg\LaserProfile.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_profile.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__BUILDER_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "crawling_robot_interfaces/msg/detail/laser_profile__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace crawling_robot_interfaces
{

namespace msg
{

namespace builder
{

class Init_LaserProfile_encoder_ticks
{
public:
  explicit Init_LaserProfile_encoder_ticks(::crawling_robot_interfaces::msg::LaserProfile & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::msg::LaserProfile encoder_ticks(::crawling_robot_interfaces::msg::LaserProfile::_encoder_ticks_type arg)
  {
    msg_.encoder_ticks = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserProfile msg_;
};

class Init_LaserProfile_points
{
public:
  explicit Init_LaserProfile_points(::crawling_robot_interfaces::msg::LaserProfile & msg)
  : msg_(msg)
  {}
  Init_LaserProfile_encoder_ticks points(::crawling_robot_interfaces::msg::LaserProfile::_points_type arg)
  {
    msg_.points = std::move(arg);
    return Init_LaserProfile_encoder_ticks(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserProfile msg_;
};

class Init_LaserProfile_header
{
public:
  Init_LaserProfile_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_LaserProfile_points header(::crawling_robot_interfaces::msg::LaserProfile::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_LaserProfile_points(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserProfile msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::msg::LaserProfile>()
{
  return crawling_robot_interfaces::msg::builder::Init_LaserProfile_header();
}

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__BUILDER_HPP_
