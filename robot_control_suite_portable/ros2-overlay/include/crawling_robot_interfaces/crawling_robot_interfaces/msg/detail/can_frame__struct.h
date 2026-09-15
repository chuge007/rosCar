// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from crawling_robot_interfaces:msg\CanFrame.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/can_frame.h"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__STRUCT_H_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"

/// Struct defined in msg/CanFrame in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__msg__CanFrame
{
  std_msgs__msg__Header header;
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} crawling_robot_interfaces__msg__CanFrame;

// Struct for a sequence of crawling_robot_interfaces__msg__CanFrame.
typedef struct crawling_robot_interfaces__msg__CanFrame__Sequence
{
  crawling_robot_interfaces__msg__CanFrame * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__msg__CanFrame__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__CAN_FRAME__STRUCT_H_
