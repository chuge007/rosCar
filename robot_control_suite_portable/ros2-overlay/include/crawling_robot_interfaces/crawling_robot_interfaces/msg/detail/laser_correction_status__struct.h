// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from crawling_robot_interfaces:msg\LaserCorrectionStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "crawling_robot_interfaces/msg/laser_correction_status.h"


#ifndef CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__STRUCT_H_
#define CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__STRUCT_H_

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

/// Struct defined in msg/LaserCorrectionStatus in the package crawling_robot_interfaces.
typedef struct crawling_robot_interfaces__msg__LaserCorrectionStatus
{
  std_msgs__msg__Header header;
  bool active;
  bool contour_valid;
  bool geometry_valid;
  double lateral_error_m;
  double preview_lateral_error_m;
  double heading_error_rad;
  double curvature_1pm;
  double angular_command_rad_s;
  double angular_accel_rad_s2;
  double linear_command_m_s;
  double contour_lateral_m;
  double confidence;
  double fit_residual_m;
  uint32_t trajectory_points;
} crawling_robot_interfaces__msg__LaserCorrectionStatus;

// Struct for a sequence of crawling_robot_interfaces__msg__LaserCorrectionStatus.
typedef struct crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence
{
  crawling_robot_interfaces__msg__LaserCorrectionStatus * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // CRAWLING_ROBOT_INTERFACES__MSG__DETAIL__LASER_CORRECTION_STATUS__STRUCT_H_
