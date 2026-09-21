// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from crawling_robot_interfaces:srv\SetMotorMapping.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_motor_mapping.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__STRUCT_HPP_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Request __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Request __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetMotorMapping_Request_
{
  using Type = SetMotorMapping_Request_<ContainerAllocator>;

  explicit SetMotorMapping_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->left_motor_id = 0;
      this->right_motor_id = 0;
      this->left_motor_sign = 0;
      this->right_motor_sign = 0;
    }
  }

  explicit SetMotorMapping_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->left_motor_id = 0;
      this->right_motor_id = 0;
      this->left_motor_sign = 0;
      this->right_motor_sign = 0;
    }
  }

  // field types and members
  using _left_motor_id_type =
    uint8_t;
  _left_motor_id_type left_motor_id;
  using _right_motor_id_type =
    uint8_t;
  _right_motor_id_type right_motor_id;
  using _left_motor_sign_type =
    int8_t;
  _left_motor_sign_type left_motor_sign;
  using _right_motor_sign_type =
    int8_t;
  _right_motor_sign_type right_motor_sign;

  // setters for named parameter idiom
  Type & set__left_motor_id(
    const uint8_t & _arg)
  {
    this->left_motor_id = _arg;
    return *this;
  }
  Type & set__right_motor_id(
    const uint8_t & _arg)
  {
    this->right_motor_id = _arg;
    return *this;
  }
  Type & set__left_motor_sign(
    const int8_t & _arg)
  {
    this->left_motor_sign = _arg;
    return *this;
  }
  Type & set__right_motor_sign(
    const int8_t & _arg)
  {
    this->right_motor_sign = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Request
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Request
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetMotorMapping_Request_ & other) const
  {
    if (this->left_motor_id != other.left_motor_id) {
      return false;
    }
    if (this->right_motor_id != other.right_motor_id) {
      return false;
    }
    if (this->left_motor_sign != other.left_motor_sign) {
      return false;
    }
    if (this->right_motor_sign != other.right_motor_sign) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetMotorMapping_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetMotorMapping_Request_

// alias to use template instance with default allocator
using SetMotorMapping_Request =
  crawling_robot_interfaces::srv::SetMotorMapping_Request_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace crawling_robot_interfaces


#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Response __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Response __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetMotorMapping_Response_
{
  using Type = SetMotorMapping_Response_<ContainerAllocator>;

  explicit SetMotorMapping_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->message = "";
    }
  }

  explicit SetMotorMapping_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : message(_alloc)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->message = "";
    }
  }

  // field types and members
  using _success_type =
    bool;
  _success_type success;
  using _message_type =
    std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>>;
  _message_type message;

  // setters for named parameter idiom
  Type & set__success(
    const bool & _arg)
  {
    this->success = _arg;
    return *this;
  }
  Type & set__message(
    const std::basic_string<char, std::char_traits<char>, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<char>> & _arg)
  {
    this->message = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Response
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Response
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetMotorMapping_Response_ & other) const
  {
    if (this->success != other.success) {
      return false;
    }
    if (this->message != other.message) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetMotorMapping_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetMotorMapping_Response_

// alias to use template instance with default allocator
using SetMotorMapping_Response =
  crawling_robot_interfaces::srv::SetMotorMapping_Response_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace crawling_robot_interfaces


// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Event __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Event __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetMotorMapping_Event_
{
  using Type = SetMotorMapping_Event_<ContainerAllocator>;

  explicit SetMotorMapping_Event_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_init)
  {
    (void)_init;
  }

  explicit SetMotorMapping_Event_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _info_type =
    service_msgs::msg::ServiceEventInfo_<ContainerAllocator>;
  _info_type info;
  using _request_type =
    rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>>;
  _request_type request;
  using _response_type =
    rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>>;
  _response_type response;

  // setters for named parameter idiom
  Type & set__info(
    const service_msgs::msg::ServiceEventInfo_<ContainerAllocator> & _arg)
  {
    this->info = _arg;
    return *this;
  }
  Type & set__request(
    const rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetMotorMapping_Request_<ContainerAllocator>>> & _arg)
  {
    this->request = _arg;
    return *this;
  }
  Type & set__response(
    const rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetMotorMapping_Response_<ContainerAllocator>>> & _arg)
  {
    this->response = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Event
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetMotorMapping_Event
    std::shared_ptr<crawling_robot_interfaces::srv::SetMotorMapping_Event_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetMotorMapping_Event_ & other) const
  {
    if (this->info != other.info) {
      return false;
    }
    if (this->request != other.request) {
      return false;
    }
    if (this->response != other.response) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetMotorMapping_Event_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetMotorMapping_Event_

// alias to use template instance with default allocator
using SetMotorMapping_Event =
  crawling_robot_interfaces::srv::SetMotorMapping_Event_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace crawling_robot_interfaces

namespace crawling_robot_interfaces
{

namespace srv
{

struct SetMotorMapping
{
  using Request = crawling_robot_interfaces::srv::SetMotorMapping_Request;
  using Response = crawling_robot_interfaces::srv::SetMotorMapping_Response;
  using Event = crawling_robot_interfaces::srv::SetMotorMapping_Event;
};

}  // namespace srv

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__STRUCT_HPP_
