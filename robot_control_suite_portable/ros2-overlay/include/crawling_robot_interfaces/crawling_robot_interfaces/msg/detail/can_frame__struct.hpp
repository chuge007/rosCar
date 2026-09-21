// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from crawling_robot_interfaces:msg\CanFrame.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/can_frame.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__STRUCT_HPP_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__STRUCT_HPP_

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
# define DEPRECATED__crawling_robot_interfaces__msg__CanFrame __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__msg__CanFrame __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct CanFrame_
{
  using Type = CanFrame_<ContainerAllocator>;

  explicit CanFrame_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_init)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->id = 0ul;
      this->dlc = 0;
      std::fill<typename std::array<uint8_t, 8>::iterator, uint8_t>(this->data.begin(), this->data.end(), 0);
    }
  }

  explicit CanFrame_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : header(_alloc, _init),
    data(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->id = 0ul;
      this->dlc = 0;
      std::fill<typename std::array<uint8_t, 8>::iterator, uint8_t>(this->data.begin(), this->data.end(), 0);
    }
  }

  // field types and members
  using _header_type =
    std_msgs::msg::Header_<ContainerAllocator>;
  _header_type header;
  using _id_type =
    uint32_t;
  _id_type id;
  using _dlc_type =
    uint8_t;
  _dlc_type dlc;
  using _data_type =
    std::array<uint8_t, 8>;
  _data_type data;

  // setters for named parameter idiom
  Type & set__header(
    const std_msgs::msg::Header_<ContainerAllocator> & _arg)
  {
    this->header = _arg;
    return *this;
  }
  Type & set__id(
    const uint32_t & _arg)
  {
    this->id = _arg;
    return *this;
  }
  Type & set__dlc(
    const uint8_t & _arg)
  {
    this->dlc = _arg;
    return *this;
  }
  Type & set__data(
    const std::array<uint8_t, 8> & _arg)
  {
    this->data = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__msg__CanFrame
    std::shared_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__msg__CanFrame
    std::shared_ptr<crawling_robot_interfaces::msg::CanFrame_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const CanFrame_ & other) const
  {
    if (this->header != other.header) {
      return false;
    }
    if (this->id != other.id) {
      return false;
    }
    if (this->dlc != other.dlc) {
      return false;
    }
    if (this->data != other.data) {
      return false;
    }
    return true;
  }
  bool operator!=(const CanFrame_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct CanFrame_

// alias to use template instance with default allocator
using CanFrame =
  crawling_robot_interfaces::msg::CanFrame_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__STRUCT_HPP_
