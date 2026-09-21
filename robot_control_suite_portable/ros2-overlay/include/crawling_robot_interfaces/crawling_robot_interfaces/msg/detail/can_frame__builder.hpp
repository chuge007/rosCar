// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from crawling_robot_interfaces:msg\CanFrame.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/can_frame.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__BUILDER_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "crawling_robot_interfaces/msg/detail/can_frame__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace crawling_robot_interfaces
{

namespace msg
{

namespace builder
{

class Init_CanFrame_data
{
public:
  explicit Init_CanFrame_data(::crawling_robot_interfaces::msg::CanFrame & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::msg::CanFrame data(::crawling_robot_interfaces::msg::CanFrame::_data_type arg)
  {
    msg_.data = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::CanFrame msg_;
};

class Init_CanFrame_dlc
{
public:
  explicit Init_CanFrame_dlc(::crawling_robot_interfaces::msg::CanFrame & msg)
  : msg_(msg)
  {}
  Init_CanFrame_data dlc(::crawling_robot_interfaces::msg::CanFrame::_dlc_type arg)
  {
    msg_.dlc = std::move(arg);
    return Init_CanFrame_data(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::CanFrame msg_;
};

class Init_CanFrame_id
{
public:
  explicit Init_CanFrame_id(::crawling_robot_interfaces::msg::CanFrame & msg)
  : msg_(msg)
  {}
  Init_CanFrame_dlc id(::crawling_robot_interfaces::msg::CanFrame::_id_type arg)
  {
    msg_.id = std::move(arg);
    return Init_CanFrame_dlc(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::CanFrame msg_;
};

class Init_CanFrame_header
{
public:
  Init_CanFrame_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_CanFrame_id header(::crawling_robot_interfaces::msg::CanFrame::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_CanFrame_id(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::CanFrame msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::msg::CanFrame>()
{
  return crawling_robot_interfaces::msg::builder::Init_CanFrame_header();
}

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__BUILDER_HPP_
