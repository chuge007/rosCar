// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from crawling_robot_interfaces:srv\SetMotorMapping.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_motor_mapping.h"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__STRUCT_H_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in srv/SetMotorMapping in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetMotorMapping_Request
{
  uint8_t left_motor_id;
  uint8_t right_motor_id;
  int8_t left_motor_sign;
  int8_t right_motor_sign;
} crawling_robot_interfaces__srv__SetMotorMapping_Request;

// Struct for a sequence of crawling_robot_interfaces__srv__SetMotorMapping_Request.
typedef struct crawling_robot_interfaces__srv__SetMotorMapping_Request__Sequence
{
  crawling_robot_interfaces__srv__SetMotorMapping_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetMotorMapping_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'message'
#include "rosidl_runtime_c/string.h"

/// Struct defined in srv/SetMotorMapping in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetMotorMapping_Response
{
  bool success;
  rosidl_runtime_c__String message;
} crawling_robot_interfaces__srv__SetMotorMapping_Response;

// Struct for a sequence of crawling_robot_interfaces__srv__SetMotorMapping_Response.
typedef struct crawling_robot_interfaces__srv__SetMotorMapping_Response__Sequence
{
  crawling_robot_interfaces__srv__SetMotorMapping_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetMotorMapping_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  crawling_robot_interfaces__srv__SetMotorMapping_Event__request__MAX_SIZE = 1
};
// response
enum
{
  crawling_robot_interfaces__srv__SetMotorMapping_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/SetMotorMapping in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetMotorMapping_Event
{
  service_msgs__msg__ServiceEventInfo info;
  crawling_robot_interfaces__srv__SetMotorMapping_Request__Sequence request;
  crawling_robot_interfaces__srv__SetMotorMapping_Response__Sequence response;
} crawling_robot_interfaces__srv__SetMotorMapping_Event;

// Struct for a sequence of crawling_robot_interfaces__srv__SetMotorMapping_Event.
typedef struct crawling_robot_interfaces__srv__SetMotorMapping_Event__Sequence
{
  crawling_robot_interfaces__srv__SetMotorMapping_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetMotorMapping_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_MOTOR_MAPPING__STRUCT_H_
