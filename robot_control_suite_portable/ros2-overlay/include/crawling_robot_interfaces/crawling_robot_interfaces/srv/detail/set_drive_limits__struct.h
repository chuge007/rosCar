// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from crawling_robot_interfaces:srv\SetDriveLimits.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_drive_limits.h"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__STRUCT_H_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in srv/SetDriveLimits in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetDriveLimits_Request
{
  double max_linear_speed_m_s;
  double max_angular_speed_rad_s;
  double max_linear_accel_m_s2;
  double max_angular_accel_rad_s2;
  /// Safety cap applied uniformly to the two physical wheel speeds.
  double max_wheel_speed_m_s;
  /// Minimum inner/outer wheel speed ratio during a translating turn.
  double minimum_inner_wheel_ratio;
} crawling_robot_interfaces__srv__SetDriveLimits_Request;

// Struct for a sequence of crawling_robot_interfaces__srv__SetDriveLimits_Request.
typedef struct crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence
{
  crawling_robot_interfaces__srv__SetDriveLimits_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'message'
#include "rosidl_runtime_c/string.h"

/// Struct defined in srv/SetDriveLimits in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetDriveLimits_Response
{
  bool success;
  rosidl_runtime_c__String message;
} crawling_robot_interfaces__srv__SetDriveLimits_Response;

// Struct for a sequence of crawling_robot_interfaces__srv__SetDriveLimits_Response.
typedef struct crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence
{
  crawling_robot_interfaces__srv__SetDriveLimits_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  crawling_robot_interfaces__srv__SetDriveLimits_Event__request__MAX_SIZE = 1
};
// response
enum
{
  crawling_robot_interfaces__srv__SetDriveLimits_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/SetDriveLimits in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetDriveLimits_Event
{
  service_msgs__msg__ServiceEventInfo info;
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence request;
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence response;
} crawling_robot_interfaces__srv__SetDriveLimits_Event;

// Struct for a sequence of crawling_robot_interfaces__srv__SetDriveLimits_Event.
typedef struct crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence
{
  crawling_robot_interfaces__srv__SetDriveLimits_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_DRIVE_LIMITS__STRUCT_H_
