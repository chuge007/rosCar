// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from crawling_robot_interfaces:msg\LaserCorrectionStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_correction_status.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__BUILDER_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "crawling_robot_interfaces/msg/detail/laser_correction_status__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace crawling_robot_interfaces
{

namespace msg
{

namespace builder
{

class Init_LaserCorrectionStatus_trajectory_points
{
public:
  explicit Init_LaserCorrectionStatus_trajectory_points(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus trajectory_points(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_trajectory_points_type arg)
  {
    msg_.trajectory_points = std::move(arg);
    return std::move(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_fit_residual_m
{
public:
  explicit Init_LaserCorrectionStatus_fit_residual_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_trajectory_points fit_residual_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_fit_residual_m_type arg)
  {
    msg_.fit_residual_m = std::move(arg);
    return Init_LaserCorrectionStatus_trajectory_points(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_confidence
{
public:
  explicit Init_LaserCorrectionStatus_confidence(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_fit_residual_m confidence(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_confidence_type arg)
  {
    msg_.confidence = std::move(arg);
    return Init_LaserCorrectionStatus_fit_residual_m(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_contour_lateral_m
{
public:
  explicit Init_LaserCorrectionStatus_contour_lateral_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_confidence contour_lateral_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_contour_lateral_m_type arg)
  {
    msg_.contour_lateral_m = std::move(arg);
    return Init_LaserCorrectionStatus_confidence(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_linear_command_m_s
{
public:
  explicit Init_LaserCorrectionStatus_linear_command_m_s(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_contour_lateral_m linear_command_m_s(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_linear_command_m_s_type arg)
  {
    msg_.linear_command_m_s = std::move(arg);
    return Init_LaserCorrectionStatus_contour_lateral_m(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_angular_accel_rad_s2
{
public:
  explicit Init_LaserCorrectionStatus_angular_accel_rad_s2(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_linear_command_m_s angular_accel_rad_s2(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_angular_accel_rad_s2_type arg)
  {
    msg_.angular_accel_rad_s2 = std::move(arg);
    return Init_LaserCorrectionStatus_linear_command_m_s(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_angular_command_rad_s
{
public:
  explicit Init_LaserCorrectionStatus_angular_command_rad_s(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_angular_accel_rad_s2 angular_command_rad_s(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_angular_command_rad_s_type arg)
  {
    msg_.angular_command_rad_s = std::move(arg);
    return Init_LaserCorrectionStatus_angular_accel_rad_s2(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_curvature_1pm
{
public:
  explicit Init_LaserCorrectionStatus_curvature_1pm(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_angular_command_rad_s curvature_1pm(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_curvature_1pm_type arg)
  {
    msg_.curvature_1pm = std::move(arg);
    return Init_LaserCorrectionStatus_angular_command_rad_s(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_heading_error_rad
{
public:
  explicit Init_LaserCorrectionStatus_heading_error_rad(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_curvature_1pm heading_error_rad(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_heading_error_rad_type arg)
  {
    msg_.heading_error_rad = std::move(arg);
    return Init_LaserCorrectionStatus_curvature_1pm(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_preview_lateral_error_m
{
public:
  explicit Init_LaserCorrectionStatus_preview_lateral_error_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_heading_error_rad preview_lateral_error_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_preview_lateral_error_m_type arg)
  {
    msg_.preview_lateral_error_m = std::move(arg);
    return Init_LaserCorrectionStatus_heading_error_rad(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_lateral_error_m
{
public:
  explicit Init_LaserCorrectionStatus_lateral_error_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_preview_lateral_error_m lateral_error_m(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_lateral_error_m_type arg)
  {
    msg_.lateral_error_m = std::move(arg);
    return Init_LaserCorrectionStatus_preview_lateral_error_m(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_geometry_valid
{
public:
  explicit Init_LaserCorrectionStatus_geometry_valid(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_lateral_error_m geometry_valid(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_geometry_valid_type arg)
  {
    msg_.geometry_valid = std::move(arg);
    return Init_LaserCorrectionStatus_lateral_error_m(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_contour_valid
{
public:
  explicit Init_LaserCorrectionStatus_contour_valid(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_geometry_valid contour_valid(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_contour_valid_type arg)
  {
    msg_.contour_valid = std::move(arg);
    return Init_LaserCorrectionStatus_geometry_valid(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_active
{
public:
  explicit Init_LaserCorrectionStatus_active(::crawling_robot_interfaces::msg::LaserCorrectionStatus & msg)
  : msg_(msg)
  {}
  Init_LaserCorrectionStatus_contour_valid active(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_active_type arg)
  {
    msg_.active = std::move(arg);
    return Init_LaserCorrectionStatus_contour_valid(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

class Init_LaserCorrectionStatus_header
{
public:
  Init_LaserCorrectionStatus_header()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_LaserCorrectionStatus_active header(::crawling_robot_interfaces::msg::LaserCorrectionStatus::_header_type arg)
  {
    msg_.header = std::move(arg);
    return Init_LaserCorrectionStatus_active(msg_);
  }

private:
  ::crawling_robot_interfaces::msg::LaserCorrectionStatus msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::crawling_robot_interfaces::msg::LaserCorrectionStatus>()
{
  return crawling_robot_interfaces::msg::builder::Init_LaserCorrectionStatus_header();
}

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__BUILDER_HPP_
