// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from crawling_robot_interfaces:msg\LaserCorrectionStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_correction_status.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__STRUCT_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__msg__LaserCorrectionStatus __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__msg__LaserCorrectionStatus __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct LaserCorrectionStatus_
{
  using Type = LaserCorrectionStatus_<ContainerAllocator>;

  explicit LaserCorrectionStatus_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->active = false;
      this->contour_valid = false;
      this->geometry_valid = false;
      this->lateral_error_m = 0.0;
      this->preview_lateral_error_m = 0.0;
      this->heading_error_rad = 0.0;
      this->curvature_1pm = 0.0;
      this->angular_command_rad_s = 0.0;
      this->angular_accel_rad_s2 = 0.0;
      this->linear_command_m_s = 0.0;
      this->contour_lateral_m = 0.0;
      this->confidence = 0.0;
      this->fit_residual_m = 0.0;
      this->trajectory_points = 0ul;
    }
  }

  explicit LaserCorrectionStatus_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->active = false;
      this->contour_valid = false;
      this->geometry_valid = false;
      this->lateral_error_m = 0.0;
      this->preview_lateral_error_m = 0.0;
      this->heading_error_rad = 0.0;
      this->curvature_1pm = 0.0;
      this->angular_command_rad_s = 0.0;
      this->angular_accel_rad_s2 = 0.0;
      this->linear_command_m_s = 0.0;
      this->contour_lateral_m = 0.0;
      this->confidence = 0.0;
      this->fit_residual_m = 0.0;
      this->trajectory_points = 0ul;
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _active_type =
    bool;
  _active_type active;
  using _contour_valid_type =
    bool;
  _contour_valid_type contour_valid;
  using _geometry_valid_type =
    bool;
  _geometry_valid_type geometry_valid;
  using _lateral_error_m_type =
    double;
  _lateral_error_m_type lateral_error_m;
  using _preview_lateral_error_m_type =
    double;
  _preview_lateral_error_m_type preview_lateral_error_m;
  using _heading_error_rad_type =
    double;
  _heading_error_rad_type heading_error_rad;
  using _curvature_1pm_type =
    double;
  _curvature_1pm_type curvature_1pm;
  using _angular_command_rad_s_type =
    double;
  _angular_command_rad_s_type angular_command_rad_s;
  using _angular_accel_rad_s2_type =
    double;
  _angular_accel_rad_s2_type angular_accel_rad_s2;
  using _linear_command_m_s_type =
    double;
  _linear_command_m_s_type linear_command_m_s;
  using _contour_lateral_m_type =
    double;
  _contour_lateral_m_type contour_lateral_m;
  using _confidence_type =
    double;
  _confidence_type confidence;
  using _fit_residual_m_type =
    double;
  _fit_residual_m_type fit_residual_m;
  using _trajectory_points_type =
    uint32_t;
  _trajectory_points_type trajectory_points;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__active(
    const bool & _arg)
  {
    this->active = _arg;
    return *this;
  }
  Type & set__contour_valid(
    const bool & _arg)
  {
    this->contour_valid = _arg;
    return *this;
  }
  Type & set__geometry_valid(
    const bool & _arg)
  {
    this->geometry_valid = _arg;
    return *this;
  }
  Type & set__lateral_error_m(
    const double & _arg)
  {
    this->lateral_error_m = _arg;
    return *this;
  }
  Type & set__preview_lateral_error_m(
    const double & _arg)
  {
    this->preview_lateral_error_m = _arg;
    return *this;
  }
  Type & set__heading_error_rad(
    const double & _arg)
  {
    this->heading_error_rad = _arg;
    return *this;
  }
  Type & set__curvature_1pm(
    const double & _arg)
  {
    this->curvature_1pm = _arg;
    return *this;
  }
  Type & set__angular_command_rad_s(
    const double & _arg)
  {
    this->angular_command_rad_s = _arg;
    return *this;
  }
  Type & set__angular_accel_rad_s2(
    const double & _arg)
  {
    this->angular_accel_rad_s2 = _arg;
    return *this;
  }
  Type & set__linear_command_m_s(
    const double & _arg)
  {
    this->linear_command_m_s = _arg;
    return *this;
  }
  Type & set__contour_lateral_m(
    const double & _arg)
  {
    this->contour_lateral_m = _arg;
    return *this;
  }
  Type & set__confidence(
    const double & _arg)
  {
    this->confidence = _arg;
    return *this;
  }
  Type & set__fit_residual_m(
    const double & _arg)
  {
    this->fit_residual_m = _arg;
    return *this;
  }
  Type & set__trajectory_points(
    const uint32_t & _arg)
  {
    this->trajectory_points = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__msg__LaserCorrectionStatus
    std::shared_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__msg__LaserCorrectionStatus
    std::shared_ptr<crawling_robot_interfaces::msg::LaserCorrectionStatus_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const LaserCorrectionStatus_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->active != other.active) {
      return false;
    }
    if (this->contour_valid != other.contour_valid) {
      return false;
    }
    if (this->geometry_valid != other.geometry_valid) {
      return false;
    }
    if (this->lateral_error_m != other.lateral_error_m) {
      return false;
    }
    if (this->preview_lateral_error_m != other.preview_lateral_error_m) {
      return false;
    }
    if (this->heading_error_rad != other.heading_error_rad) {
      return false;
    }
    if (this->curvature_1pm != other.curvature_1pm) {
      return false;
    }
    if (this->angular_command_rad_s != other.angular_command_rad_s) {
      return false;
    }
    if (this->angular_accel_rad_s2 != other.angular_accel_rad_s2) {
      return false;
    }
    if (this->linear_command_m_s != other.linear_command_m_s) {
      return false;
    }
    if (this->contour_lateral_m != other.contour_lateral_m) {
      return false;
    }
    if (this->confidence != other.confidence) {
      return false;
    }
    if (this->fit_residual_m != other.fit_residual_m) {
      return false;
    }
    if (this->trajectory_points != other.trajectory_points) {
      return false;
    }
    return true;
  }
  bool operator!=(const LaserCorrectionStatus_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct LaserCorrectionStatus_

// alias to use template instance with default allocator
using LaserCorrectionStatus =
  crawling_robot_interfaces::msg::LaserCorrectionStatus_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__STRUCT_HPP_
