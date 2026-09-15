// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from crawling_robot_interfaces:srv\SetLocalizationReference.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/srv/set_localization_reference.h"


#ifndef CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__STRUCT_H_
#define CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in srv/SetLocalizationReference in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetLocalizationReference_Request
{
  double contour_lateral_m;
  double heading_reference_rad;
} crawling_robot_interfaces__srv__SetLocalizationReference_Request;

// Struct for a sequence of crawling_robot_interfaces__srv__SetLocalizationReference_Request.
typedef struct crawling_robot_interfaces__srv__SetLocalizationReference_Request__Sequence
{
  crawling_robot_interfaces__srv__SetLocalizationReference_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetLocalizationReference_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'message'
#include "rosidl_runtime_c/string.h"

/// Struct defined in srv/SetLocalizationReference in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetLocalizationReference_Response
{
  bool success;
  rosidl_runtime_c__String message;
} crawling_robot_interfaces__srv__SetLocalizationReference_Response;

// Struct for a sequence of crawling_robot_interfaces__srv__SetLocalizationReference_Response.
typedef struct crawling_robot_interfaces__srv__SetLocalizationReference_Response__Sequence
{
  crawling_robot_interfaces__srv__SetLocalizationReference_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetLocalizationReference_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  crawling_robot_interfaces__srv__SetLocalizationReference_Event__request__MAX_SIZE = 1
};
// response
enum
{
  crawling_robot_interfaces__srv__SetLocalizationReference_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/SetLocalizationReference in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__srv__SetLocalizationReference_Event
{
  service_msgs__msg__ServiceEventInfo info;
  crawling_robot_interfaces__srv__SetLocalizationReference_Request__Sequence request;
  crawling_robot_interfaces__srv__SetLocalizationReference_Response__Sequence response;
} crawling_robot_interfaces__srv__SetLocalizationReference_Event;

// Struct for a sequence of crawling_robot_interfaces__srv__SetLocalizationReference_Event.
typedef struct crawling_robot_interfaces__srv__SetLocalizationReference_Event__Sequence
{
  crawling_robot_interfaces__srv__SetLocalizationReference_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__srv__SetLocalizationReference_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__SRV__DETAIL__SET_LOCALIZATION_REFERENCE__STRUCT_H_
