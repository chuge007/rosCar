// generated from rosidl_typesupport_fastrtps_c/resource/idl__rosidl_typesupport_fastrtps_c.h.em
// with input from crawling_robot_interfaces:msg\CanFrame.idl
// generated code does not contain a copyright notice
#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_


#include <stddef.h>
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_typesupport_interface/macros.h"
#include "crawling_robot_interfaces/msg/rosidl_typesupport_fastrtps_c__visibility_control.h"
#include "crawling_robot_interfaces/msg/detail/can_frame__struct.h"
#include "fastcdr/Cdr.h"

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
bool cdr_serialize_crawling_robot_interfaces__msg__CanFrame(
  const crawling_robot_interfaces__msg__CanFrame * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
bool cdr_deserialize_crawling_robot_interfaces__msg__CanFrame(
  eprosima::fastcdr::Cdr &,
  crawling_robot_interfaces__msg__CanFrame * ros_message);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
size_t get_serialized_size_crawling_robot_interfaces__msg__CanFrame(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
size_t max_serialized_size_crawling_robot_interfaces__msg__CanFrame(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
bool cdr_serialize_key_crawling_robot_interfaces__msg__CanFrame(
  const crawling_robot_interfaces__msg__CanFrame * ros_message,
  eprosima::fastcdr::Cdr & cdr);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
size_t get_serialized_size_key_crawling_robot_interfaces__msg__CanFrame(
  const void * untyped_ros_message,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
size_t max_serialized_size_key_crawling_robot_interfaces__msg__CanFrame(
  bool & full_bounded,
  bool & is_plain,
  size_t current_alignment);

ROSIDL_TYPESUPPORT_FASTRTPS_C_PUBLIC_crawling_robot_interfaces
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, crawling_robot_interfaces, msg, CanFrame)();

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__ROSIDL_TYPESUPPORT_FASTRTPS_C_H_
