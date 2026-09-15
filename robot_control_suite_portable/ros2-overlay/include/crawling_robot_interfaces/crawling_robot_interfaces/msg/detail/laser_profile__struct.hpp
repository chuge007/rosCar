// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from crawling_robot_interfaces:msg\LaserProfile.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_profile.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__STRUCT_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__STRUCT_HPP_

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
// Member 'points'
#include "sensor_msgs/msg/detail/point_cloud2__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__msg__LaserProfile __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__msg__LaserProfile __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct LaserProfile_
{
  using Type = LaserProfile_<ContainerAllocator>;

  explicit LaserProfile_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init),
    points(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->encoder_ticks = 0ll;
    }
  }

  explicit LaserProfile_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init),
    points(_alloc, _init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->encoder_ticks = 0ll;
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _points_type =
    sensor_msgs::msg::PointCloud2_<ContainerAllocator>;
  _points_type points;
  using _encoder_ticks_type =
    int64_t;
  _encoder_ticks_type encoder_ticks;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__points(
    const sensor_msgs::msg::PointCloud2_<ContainerAllocator> & _arg)
  {
    this->points = _arg;
    return *this;
  }
  Type & set__encoder_ticks(
    const int64_t & _arg)
  {
    this->encoder_ticks = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__msg__LaserProfile
    std::shared_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__msg__LaserProfile
    std::shared_ptr<crawling_robot_interfaces::msg::LaserProfile_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const LaserProfile_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->points != other.points) {
      return false;
    }
    if (this->encoder_ticks != other.encoder_ticks) {
      return false;
    }
    return true;
  }
  bool operator!=(const LaserProfile_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct LaserProfile_

// alias to use template instance with default allocator
using LaserProfile =
  crawling_robot_interfaces::msg::LaserProfile_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__STRUCT_HPP_
