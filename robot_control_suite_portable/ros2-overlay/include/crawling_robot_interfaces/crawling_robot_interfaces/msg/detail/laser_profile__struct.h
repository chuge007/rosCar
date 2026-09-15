// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from crawling_robot_interfaces:msg\LaserProfile.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_profile.h"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__STRUCT_H_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__STRUCT_H_

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
// Member 'points'
#include "sensor_msgs/msg/detail/point_cloud2__struct.h"

/// Struct defined in msg/LaserProfile in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__msg__LaserProfile
{
  std_msgs__msg__Header header;
  sensor_msgs__msg__PointCloud2 points;
  int64_t encoder_ticks;
} crawling_robot_interfaces__msg__LaserProfile;

// Struct for a sequence of crawling_robot_interfaces__msg__LaserProfile.
typedef struct crawling_robot_interfaces__msg__LaserProfile__Sequence
{
  crawling_robot_interfaces__msg__LaserProfile * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__msg__LaserProfile__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_PROFILE__STRUCT_H_
