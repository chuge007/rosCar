// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from crawling_robot_interfaces:srv\SetLocalizationReference.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_localization_reference.hpp"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__STRUCT_HPP_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Request __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Request __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetLocalizationReference_Request_
{
  using Type = SetLocalizationReference_Request_<ContainerAllocator>;

  explicit SetLocalizationReference_Request_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->contour_lateral_m = 0.0;
      this->heading_reference_rad = 0.0;
    }
  }

  explicit SetLocalizationReference_Request_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->contour_lateral_m = 0.0;
      this->heading_reference_rad = 0.0;
    }
  }

  // field types and members
  using _contour_lateral_m_type =
    double;
  _contour_lateral_m_type contour_lateral_m;
  using _heading_reference_rad_type =
    double;
  _heading_reference_rad_type heading_reference_rad;

  // setters for named parameter idiom
  Type & set__contour_lateral_m(
    const double & _arg)
  {
    this->contour_lateral_m = _arg;
    return *this;
  }
  Type & set__heading_reference_rad(
    const double & _arg)
  {
    this->heading_reference_rad = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Request
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Request
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetLocalizationReference_Request_ & other) const
  {
    if (this->contour_lateral_m != other.contour_lateral_m) {
      return false;
    }
    if (this->heading_reference_rad != other.heading_reference_rad) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetLocalizationReference_Request_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetLocalizationReference_Request_

// alias to use template instance with default allocator
using SetLocalizationReference_Request =
  crawling_robot_interfaces::srv::SetLocalizationReference_Request_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace crawling_robot_interfaces


#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Response __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Response __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetLocalizationReference_Response_
{
  using Type = SetLocalizationReference_Response_<ContainerAllocator>;

  explicit SetLocalizationReference_Response_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->success = false;
      this->message = "";
    }
  }

  explicit SetLocalizationReference_Response_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
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
    crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Response
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Response
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetLocalizationReference_Response_ & other) const
  {
    if (this->success != other.success) {
      return false;
    }
    if (this->message != other.message) {
      return false;
    }
    return true;
  }
  bool operator!=(const SetLocalizationReference_Response_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetLocalizationReference_Response_

// alias to use template instance with default allocator
using SetLocalizationReference_Response =
  crawling_robot_interfaces::srv::SetLocalizationReference_Response_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace crawling_robot_interfaces


// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.hpp"

#ifndef _WIN32
# define DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Event __attribute__((deprecated))
#else
# define DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Event __declspec(deprecated)
#endif

namespace crawling_robot_interfaces
{

namespace srv
{

// message struct
template<class ContainerAllocator>
struct SetLocalizationReference_Event_
{
  using Type = SetLocalizationReference_Event_<ContainerAllocator>;

  explicit SetLocalizationReference_Event_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_init)
  {
    (void)_init;
  }

  explicit SetLocalizationReference_Event_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  : info(_alloc, _init)
  {
    (void)_init;
  }

  // field types and members
  using _info_type =
    service_msgs::msg::ServiceEventInfo_<ContainerAllocator>;
  _info_type info;
  using _request_type =
    rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>>;
  _request_type request;
  using _response_type =
    rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>>;
  _response_type response;

  // setters for named parameter idiom
  Type & set__info(
    const service_msgs::msg::ServiceEventInfo_<ContainerAllocator> & _arg)
  {
    this->info = _arg;
    return *this;
  }
  Type & set__request(
    const rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetLocalizationReference_Request_<ContainerAllocator>>> & _arg)
  {
    this->request = _arg;
    return *this;
  }
  Type & set__response(
    const rosidl_runtime_cpp::BoundedVector<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>, 1, typename std::allocator_traits<ContainerAllocator>::template rebind_alloc<crawling_robot_interfaces::srv::SetLocalizationReference_Response_<ContainerAllocator>>> & _arg)
  {
    this->response = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator> *;
  using ConstRawPtr =
    const crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Event
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__crawling_robot_interfaces__srv__SetLocalizationReference_Event
    std::shared_ptr<crawling_robot_interfaces::srv::SetLocalizationReference_Event_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const SetLocalizationReference_Event_ & other) const
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
  bool operator!=(const SetLocalizationReference_Event_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct SetLocalizationReference_Event_

// alias to use template instance with default allocator
using SetLocalizationReference_Event =
  crawling_robot_interfaces::srv::SetLocalizationReference_Event_<std::allocator<void>>;

// constant definitions

}  // namespace srv

}  // namespace crawling_robot_interfaces

namespace crawling_robot_interfaces
{

namespace srv
{

struct SetLocalizationReference
{
  using Request = crawling_robot_interfaces::srv::SetLocalizationReference_Request;
  using Response = crawling_robot_interfaces::srv::SetLocalizationReference_Response;
  using Event = crawling_robot_interfaces::srv::SetLocalizationReference_Event;
};

}  // namespace srv

}  // namespace crawling_robot_interfaces

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__STRUCT_HPP_
