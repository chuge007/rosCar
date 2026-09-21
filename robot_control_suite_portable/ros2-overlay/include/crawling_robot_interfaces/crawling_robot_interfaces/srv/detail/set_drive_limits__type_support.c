// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from crawling_robot_interfaces:srv\SetDriveLimits.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "crawling_robot_interfaces/srv/detail/set_drive_limits__rosidl_typesupport_introspection_c.h"
#include "crawling_robot_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "crawling_robot_interfaces/srv/detail/set_drive_limits__functions.h"
#include "crawling_robot_interfaces/srv/detail/set_drive_limits__struct.h"


#ifdef __cplusplus
extern "C"
{
#endif

void crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  crawling_robot_interfaces__srv__SetDriveLimits_Request__init(message_memory);
}

void crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_fini_function(void * message_memory)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Request__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_member_array[6] = {
  {
    "max_linear_speed_m_s",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Request, max_linear_speed_m_s),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "max_angular_speed_rad_s",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Request, max_angular_speed_rad_s),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "max_linear_accel_m_s2",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Request, max_linear_accel_m_s2),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "max_angular_accel_rad_s2",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Request, max_angular_accel_rad_s2),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "max_wheel_speed_m_s",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Request, max_wheel_speed_m_s),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "minimum_inner_wheel_ratio",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Request, minimum_inner_wheel_ratio),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_members = {
  "crawling_robot_interfaces__srv",  // message namespace
  "SetDriveLimits_Request",  // message name
  6,  // number of fields
  sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Request),
  false,  // has_any_key_member_
  crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_member_array,  // message members
  crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_init_function,  // function to initialize message memory (memory has to be allocated)
  crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_type_support_handle = {
  0,
  &crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_members,
  get_message_typesupport_handle_function,
  &crawling_robot_interfaces__srv__SetDriveLimits_Request__get_type_hash,
  &crawling_robot_interfaces__srv__SetDriveLimits_Request__get_type_description,
  &crawling_robot_interfaces__srv__SetDriveLimits_Request__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_crawling_robot_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Request)() {
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_type_support_handle.typesupport_identifier) {
    crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif

// already included above
// #include <stddef.h>
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__rosidl_typesupport_introspection_c.h"
// already included above
// #include "crawling_robot_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
// already included above
// #include "rosidl_typesupport_introspection_c/field_types.h"
// already included above
// #include "rosidl_typesupport_introspection_c/identifier.h"
// already included above
// #include "rosidl_typesupport_introspection_c/message_introspection.h"
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__functions.h"
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__struct.h"


// Include directives for member types
// Member `message`
#include "rosidl_runtime_c/string_functions.h"

#ifdef __cplusplus
extern "C"
{
#endif

void crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  crawling_robot_interfaces__srv__SetDriveLimits_Response__init(message_memory);
}

void crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_fini_function(void * message_memory)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_member_array[2] = {
  {
    "success",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Response, success),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "message",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_STRING,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Response, message),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_members = {
  "crawling_robot_interfaces__srv",  // message namespace
  "SetDriveLimits_Response",  // message name
  2,  // number of fields
  sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Response),
  false,  // has_any_key_member_
  crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_member_array,  // message members
  crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_init_function,  // function to initialize message memory (memory has to be allocated)
  crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle = {
  0,
  &crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_members,
  get_message_typesupport_handle_function,
  &crawling_robot_interfaces__srv__SetDriveLimits_Response__get_type_hash,
  &crawling_robot_interfaces__srv__SetDriveLimits_Response__get_type_description,
  &crawling_robot_interfaces__srv__SetDriveLimits_Response__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_crawling_robot_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Response)() {
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle.typesupport_identifier) {
    crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif

// already included above
// #include <stddef.h>
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__rosidl_typesupport_introspection_c.h"
// already included above
// #include "crawling_robot_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
// already included above
// #include "rosidl_typesupport_introspection_c/field_types.h"
// already included above
// #include "rosidl_typesupport_introspection_c/identifier.h"
// already included above
// #include "rosidl_typesupport_introspection_c/message_introspection.h"
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__functions.h"
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__struct.h"


// Include directives for member types
// Member `info`
#include "service_msgs/msg/service_event_info.h"
// Member `info`
#include "service_msgs/msg/detail/service_event_info__rosidl_typesupport_introspection_c.h"
// Member `request`
// Member `response`
#include "crawling_robot_interfaces/srv/set_drive_limits.h"
// Member `request`
// Member `response`
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__rosidl_typesupport_introspection_c.h"

#ifdef __cplusplus
extern "C"
{
#endif

void crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  crawling_robot_interfaces__srv__SetDriveLimits_Event__init(message_memory);
}

void crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_fini_function(void * message_memory)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(message_memory);
}

size_t crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__size_function__SetDriveLimits_Event__request(
  const void * untyped_member)
{
  const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * member =
    (const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence *)(untyped_member);
  return member->size;
}

const void * crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_const_function__SetDriveLimits_Event__request(
  const void * untyped_member, size_t index)
{
  const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * member =
    (const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence *)(untyped_member);
  return &member->data[index];
}

void * crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_function__SetDriveLimits_Event__request(
  void * untyped_member, size_t index)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * member =
    (crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence *)(untyped_member);
  return &member->data[index];
}

void crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__fetch_function__SetDriveLimits_Event__request(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const crawling_robot_interfaces__srv__SetDriveLimits_Request * item =
    ((const crawling_robot_interfaces__srv__SetDriveLimits_Request *)
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_const_function__SetDriveLimits_Event__request(untyped_member, index));
  crawling_robot_interfaces__srv__SetDriveLimits_Request * value =
    (crawling_robot_interfaces__srv__SetDriveLimits_Request *)(untyped_value);
  *value = *item;
}

void crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__assign_function__SetDriveLimits_Event__request(
  void * untyped_member, size_t index, const void * untyped_value)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Request * item =
    ((crawling_robot_interfaces__srv__SetDriveLimits_Request *)
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_function__SetDriveLimits_Event__request(untyped_member, index));
  const crawling_robot_interfaces__srv__SetDriveLimits_Request * value =
    (const crawling_robot_interfaces__srv__SetDriveLimits_Request *)(untyped_value);
  *item = *value;
}

bool crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__resize_function__SetDriveLimits_Event__request(
  void * untyped_member, size_t size)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * member =
    (crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence *)(untyped_member);
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__fini(member);
  return crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__init(member, size);
}

size_t crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__size_function__SetDriveLimits_Event__response(
  const void * untyped_member)
{
  const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * member =
    (const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence *)(untyped_member);
  return member->size;
}

const void * crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_const_function__SetDriveLimits_Event__response(
  const void * untyped_member, size_t index)
{
  const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * member =
    (const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence *)(untyped_member);
  return &member->data[index];
}

void * crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_function__SetDriveLimits_Event__response(
  void * untyped_member, size_t index)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * member =
    (crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence *)(untyped_member);
  return &member->data[index];
}

void crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__fetch_function__SetDriveLimits_Event__response(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const crawling_robot_interfaces__srv__SetDriveLimits_Response * item =
    ((const crawling_robot_interfaces__srv__SetDriveLimits_Response *)
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_const_function__SetDriveLimits_Event__response(untyped_member, index));
  crawling_robot_interfaces__srv__SetDriveLimits_Response * value =
    (crawling_robot_interfaces__srv__SetDriveLimits_Response *)(untyped_value);
  *value = *item;
}

void crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__assign_function__SetDriveLimits_Event__response(
  void * untyped_member, size_t index, const void * untyped_value)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Response * item =
    ((crawling_robot_interfaces__srv__SetDriveLimits_Response *)
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_function__SetDriveLimits_Event__response(untyped_member, index));
  const crawling_robot_interfaces__srv__SetDriveLimits_Response * value =
    (const crawling_robot_interfaces__srv__SetDriveLimits_Response *)(untyped_value);
  *item = *value;
}

bool crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__resize_function__SetDriveLimits_Event__response(
  void * untyped_member, size_t size)
{
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * member =
    (crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence *)(untyped_member);
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__fini(member);
  return crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__init(member, size);
}

static rosidl_typesupport_introspection_c__MessageMember crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_member_array[3] = {
  {
    "info",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Event, info),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "request",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    true,  // is array
    1,  // array size
    true,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Event, request),  // bytes offset in struct
    NULL,  // default value
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__size_function__SetDriveLimits_Event__request,  // size() function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_const_function__SetDriveLimits_Event__request,  // get_const(index) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_function__SetDriveLimits_Event__request,  // get(index) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__fetch_function__SetDriveLimits_Event__request,  // fetch(index, &value) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__assign_function__SetDriveLimits_Event__request,  // assign(index, value) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__resize_function__SetDriveLimits_Event__request  // resize(index) function pointer
  },
  {
    "response",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    true,  // is array
    1,  // array size
    true,  // is upper bound
    offsetof(crawling_robot_interfaces__srv__SetDriveLimits_Event, response),  // bytes offset in struct
    NULL,  // default value
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__size_function__SetDriveLimits_Event__response,  // size() function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_const_function__SetDriveLimits_Event__response,  // get_const(index) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__get_function__SetDriveLimits_Event__response,  // get(index) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__fetch_function__SetDriveLimits_Event__response,  // fetch(index, &value) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__assign_function__SetDriveLimits_Event__response,  // assign(index, value) function pointer
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__resize_function__SetDriveLimits_Event__response  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_members = {
  "crawling_robot_interfaces__srv",  // message namespace
  "SetDriveLimits_Event",  // message name
  3,  // number of fields
  sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Event),
  false,  // has_any_key_member_
  crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_member_array,  // message members
  crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_init_function,  // function to initialize message memory (memory has to be allocated)
  crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_type_support_handle = {
  0,
  &crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_members,
  get_message_typesupport_handle_function,
  &crawling_robot_interfaces__srv__SetDriveLimits_Event__get_type_hash,
  &crawling_robot_interfaces__srv__SetDriveLimits_Event__get_type_description,
  &crawling_robot_interfaces__srv__SetDriveLimits_Event__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_crawling_robot_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Event)() {
  crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_member_array[0].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, service_msgs, msg, ServiceEventInfo)();
  crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_member_array[1].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Request)();
  crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_member_array[2].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Response)();
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_type_support_handle.typesupport_identifier) {
    crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif

#include "rosidl_runtime_c/service_type_support_struct.h"
// already included above
// #include "crawling_robot_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__rosidl_typesupport_introspection_c.h"
// already included above
// #include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/service_introspection.h"

// this is intentionally not const to allow initialization later to prevent an initialization race
static rosidl_typesupport_introspection_c__ServiceMembers crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_members = {
  "crawling_robot_interfaces__srv",  // service namespace
  "SetDriveLimits",  // service name
  // the following fields are initialized below on first access
  NULL,  // request message
  // crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_type_support_handle,
  NULL,  // response message
  // crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle
  NULL  // event_message
  // crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle
};


static rosidl_service_type_support_t crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_type_support_handle = {
  0,
  &crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_members,
  get_service_typesupport_handle_function,
  &crawling_robot_interfaces__srv__SetDriveLimits_Request__rosidl_typesupport_introspection_c__SetDriveLimits_Request_message_type_support_handle,
  &crawling_robot_interfaces__srv__SetDriveLimits_Response__rosidl_typesupport_introspection_c__SetDriveLimits_Response_message_type_support_handle,
  &crawling_robot_interfaces__srv__SetDriveLimits_Event__rosidl_typesupport_introspection_c__SetDriveLimits_Event_message_type_support_handle,
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_CREATE_EVENT_MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_c,
    crawling_robot_interfaces,
    srv,
    SetDriveLimits
  ),
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_DESTROY_EVENT_MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_c,
    crawling_robot_interfaces,
    srv,
    SetDriveLimits
  ),
  &crawling_robot_interfaces__srv__SetDriveLimits__get_type_hash,
  &crawling_robot_interfaces__srv__SetDriveLimits__get_type_description,
  &crawling_robot_interfaces__srv__SetDriveLimits__get_type_description_sources,
};

// Forward declaration of message type support functions for service members
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Request)(void);

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Response)(void);

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Event)(void);

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_crawling_robot_interfaces
const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits)(void) {
  if (!crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_type_support_handle.typesupport_identifier) {
    crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  rosidl_typesupport_introspection_c__ServiceMembers * service_members =
    (rosidl_typesupport_introspection_c__ServiceMembers *)crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_type_support_handle.data;

  if (!service_members->request_members_) {
    service_members->request_members_ =
      (const rosidl_typesupport_introspection_c__MessageMembers *)
      ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Request)()->data;
  }
  if (!service_members->response_members_) {
    service_members->response_members_ =
      (const rosidl_typesupport_introspection_c__MessageMembers *)
      ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Response)()->data;
  }
  if (!service_members->event_members_) {
    service_members->event_members_ =
      (const rosidl_typesupport_introspection_c__MessageMembers *)
      ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, srv, SetDriveLimits_Event)()->data;
  }

  return &crawling_robot_interfaces__srv__detail__set_drive_limits__rosidl_typesupport_introspection_c__SetDriveLimits_service_type_support_handle;
}
