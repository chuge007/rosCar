// generated from rosidl_typesupport_introspection_c/resource/idl__type_support.c.em
// with input from crawling_robot_interfaces:msg\LaserProfile.idl
// generated code does not contain a copyright notice

#include <stddef.h>
#include "crawling_robot_interfaces/msg/detail/laser_profile__rosidl_typesupport_introspection_c.h"
#include "crawling_robot_interfaces/msg/rosidl_typesupport_introspection_c__visibility_control.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "crawling_robot_interfaces/msg/detail/laser_profile__functions.h"
#include "crawling_robot_interfaces/msg/detail/laser_profile__struct.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/header.h"
// Member `header`
#include "std_msgs/msg/detail/header__rosidl_typesupport_introspection_c.h"
// Member `points`
#include "sensor_msgs/msg/point_cloud2.h"
// Member `points`
#include "sensor_msgs/msg/detail/point_cloud2__rosidl_typesupport_introspection_c.h"

#ifdef __cplusplus
extern "C"
{
#endif

void crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_init_function(
  void * message_memory, enum rosidl_runtime_c__message_initialization _init)
{
  // TODO(karsten1987): initializers are not yet implemented for typesupport c
  // see https://github.com/ros2/ros2/issues/397
  (void) _init;
  crawling_robot_interfaces__msg__LaserProfile__init(message_memory);
}

void crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_fini_function(void * message_memory)
{
  crawling_robot_interfaces__msg__LaserProfile__fini(message_memory);
}

static rosidl_typesupport_introspection_c__MessageMember crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_member_array[3] = {
  {
    "header",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__LaserProfile, header),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "points",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE,  // type
    0,  // upper bound of string
    NULL,  // members of sub message (initialized later)
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__LaserProfile, points),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  },
  {
    "encoder_ticks",  // name
    rosidl_typesupport_introspection_c__ROS_TYPE_INT64,  // type
    0,  // upper bound of string
    NULL,  // members of sub message
    false,  // is key
    false,  // is array
    0,  // array size
    false,  // is upper bound
    offsetof(crawling_robot_interfaces__msg__LaserProfile, encoder_ticks),  // bytes offset in struct
    NULL,  // default value
    NULL,  // size() function pointer
    NULL,  // get_const(index) function pointer
    NULL,  // get(index) function pointer
    NULL,  // fetch(index, &value) function pointer
    NULL,  // assign(index, value) function pointer
    NULL  // resize(index) function pointer
  }
};

static const rosidl_typesupport_introspection_c__MessageMembers crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_members = {
  "crawling_robot_interfaces__msg",  // message namespace
  "LaserProfile",  // message name
  3,  // number of fields
  sizeof(crawling_robot_interfaces__msg__LaserProfile),
  false,  // has_any_key_member_
  crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_member_array,  // message members
  crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_init_function,  // function to initialize message memory (memory has to be allocated)
  crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_fini_function  // function to terminate message instance (will not free memory)
};

// this is not const since it must be initialized on first access
// since C does not allow non-integral compile-time constants
static rosidl_message_type_support_t crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_type_support_handle = {
  0,
  &crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_members,
  get_message_typesupport_handle_function,
  &crawling_robot_interfaces__msg__LaserProfile__get_type_hash,
  &crawling_robot_interfaces__msg__LaserProfile__get_type_description,
  &crawling_robot_interfaces__msg__LaserProfile__get_type_description_sources,
};

ROSIDL_TYPESUPPORT_INTROSPECTION_C_EXPORT_crawling_robot_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, crawling_robot_interfaces, msg, LaserProfile)() {
  crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_member_array[0].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, std_msgs, msg, Header)();
  crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_member_array[1].members_ =
    ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, sensor_msgs, msg, PointCloud2)();
  if (!crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_type_support_handle.typesupport_identifier) {
    crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_type_support_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  }
  return &crawling_robot_interfaces__msg__LaserProfile__rosidl_typesupport_introspection_c__LaserProfile_message_type_support_handle;
}
#ifdef __cplusplus
}
#endif
