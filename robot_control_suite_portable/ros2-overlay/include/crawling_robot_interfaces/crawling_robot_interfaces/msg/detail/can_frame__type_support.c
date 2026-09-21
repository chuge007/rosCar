// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from crawling_robot_interfaces:msg\CanFrame.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "crawling_robot_interfaces/msg/detail/can_frame__rosidl_typesupport_introspection_c.h"
#include "crawling_robot_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "crawling_robot_interfaces/msg/detail/can_frame__functions.h"
#include "crawling_robot_interfaces/msg/detail/can_frame__struct.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/header.h"
// Member `header`
#include "std_msgs/msg/detail/header__rosidl_typesupport_introspection_c.h"

#ifdef __cplusplus
extern "C"
{
#endif

void crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  crawling_robot_interfaces__msg__CanFrame__init(message_memory);
}

void crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_fini_function(void * message_memory)
{
  crawling_robot_interfaces__msg__CanFrame__fini(message_memory);
}

size_t crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__size_function__CanFrame__data(
  const void * untyped_member)
{
  (void)untyped_member;
  return 8;
}

const void * crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__get_const_function__CanFrame__data(
  const void * untyped_member, size_t index)
{
  const uint8_t * member =
    (const uint8_t *)(untyped_member);
  return &member[index];
}

void * crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__get_function__CanFrame__data(
  void * untyped_member, size_t index)
{
  uint8_t * member =
    (uint8_t *)(untyped_member);
  return &member[index];
}

void crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__fetch_function__CanFrame__data(
  const void * untyped_member, size_t index, void * untyped_value)
{
  const uint8_t * item =
    ((const uint8_t *)
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__get_const_function__CanFrame__data(untyped_member, index));
  uint8_t * value =
    (uint8_t *)(untyped_value);
  *value = *item;
}

void crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__assign_function__CanFrame__data(
  void * untyped_member, size_t index, const void * untyped_value)
{
  uint8_t * item =
    ((uint8_t *)
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__get_function__CanFrame__data(untyped_member, index));
  const uint8_t * value =
    (const uint8_t *)(untyped_value);
  *item = *value;
}

static rosidl_typesupport_introspection_c__MessageMember crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_member_array[4] = {
  {
    "header",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__CanFrame, header),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "id",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_UINT32,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__CanFrame, id),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "dlc",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_UINT8,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__CanFrame, dlc),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "data",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_UINT8,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    true,  // is array
    8,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__CanFrame, data),  // bytes offset in struct
    NULL,  // default value
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__size_function__CanFrame__data,  // size() function pointer
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__get_const_function__CanFrame__data,  // get_const(index) function pointer
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__get_function__CanFrame__data,  // get(index) function pointer
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__fetch_function__CanFrame__data,  // fetch(index, &value) function pointer
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__assign_function__CanFrame__data,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_members = {
  "crawling_robot_interfaces__msg",  // message namespace
  "CanFrame",  // message name
  4,  // number of fields
  sizeof(crawling_robot_interfaces__msg__CanFrame),
  false,  // has_any_key_member_
  crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_member_array,  // message members
  crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_init_function,  // function to initialize message memory (memory has to be allocated)
  crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_type_support_handle = {
  0,
  &crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_members,
  get_message_typesupport_handle_function,
  &crawling_robot_interfaces__msg__CanFrame__get_type_hash,
  &crawling_robot_interfaces__msg__CanFrame__get_type_description,
  &crawling_robot_interfaces__msg__CanFrame__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_crawling_robot_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, msg, CanFrame)() {
  crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_member_array[0].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, std_msgs, msg, Header)();
  if (!crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_type_support_handle.typesupport_identifier) {
    crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &crawling_robot_interfaces__msg__CanFrame__rosidl_typesupport_introspection_c__CanFrame_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif
