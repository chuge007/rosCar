// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from crawling_robot_interfaces:msg\LaserCorrectionStatus.idl
// generated code does not contain a copyright notice
#include "crawling_robot_interfaces/msg/detail/laser_correction_status__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/detail/header__functions.h"

bool
crawling_robot_interfaces__msg__LaserCorrectionStatus__init(crawling_robot_interfaces__msg__LaserCorrectionStatus * msg)
{
  if (!msg) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__init(&msg->header)) {
    crawling_robot_interfaces__msg__LaserCorrectionStatus__fini(msg);
    return false;
  }
  // active
  // contour_valid
  // geometry_valid
  // lateral_error_m
  // preview_lateral_error_m
  // heading_error_rad
  // curvature_1pm
  // angular_command_rad_s
  // angular_accel_rad_s2
  // linear_command_m_s
  // contour_lateral_m
  // confidence
  // fit_residual_m
  // trajectory_points
  return true;
}

void
crawling_robot_interfaces__msg__LaserCorrectionStatus__fini(crawling_robot_interfaces__msg__LaserCorrectionStatus * msg)
{
  if (!msg) {
    return;
  }
  // header
  std_msgs__msg__Header__fini(&msg->header);
  // active
  // contour_valid
  // geometry_valid
  // lateral_error_m
  // preview_lateral_error_m
  // heading_error_rad
  // curvature_1pm
  // angular_command_rad_s
  // angular_accel_rad_s2
  // linear_command_m_s
  // contour_lateral_m
  // confidence
  // fit_residual_m
  // trajectory_points
}

bool
crawling_robot_interfaces__msg__LaserCorrectionStatus__are_equal(const crawling_robot_interfaces__msg__LaserCorrectionStatus * lhs, const crawling_robot_interfaces__msg__LaserCorrectionStatus * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__are_equal(
      &(lhs->header), &(rhs->header)))
  {
    return false;
  }
  // active
  if (lhs->active != rhs->active) {
    return false;
  }
  // contour_valid
  if (lhs->contour_valid != rhs->contour_valid) {
    return false;
  }
  // geometry_valid
  if (lhs->geometry_valid != rhs->geometry_valid) {
    return false;
  }
  // lateral_error_m
  if (lhs->lateral_error_m != rhs->lateral_error_m) {
    return false;
  }
  // preview_lateral_error_m
  if (lhs->preview_lateral_error_m != rhs->preview_lateral_error_m) {
    return false;
  }
  // heading_error_rad
  if (lhs->heading_error_rad != rhs->heading_error_rad) {
    return false;
  }
  // curvature_1pm
  if (lhs->curvature_1pm != rhs->curvature_1pm) {
    return false;
  }
  // angular_command_rad_s
  if (lhs->angular_command_rad_s != rhs->angular_command_rad_s) {
    return false;
  }
  // angular_accel_rad_s2
  if (lhs->angular_accel_rad_s2 != rhs->angular_accel_rad_s2) {
    return false;
  }
  // linear_command_m_s
  if (lhs->linear_command_m_s != rhs->linear_command_m_s) {
    return false;
  }
  // contour_lateral_m
  if (lhs->contour_lateral_m != rhs->contour_lateral_m) {
    return false;
  }
  // confidence
  if (lhs->confidence != rhs->confidence) {
    return false;
  }
  // fit_residual_m
  if (lhs->fit_residual_m != rhs->fit_residual_m) {
    return false;
  }
  // trajectory_points
  if (lhs->trajectory_points != rhs->trajectory_points) {
    return false;
  }
  return true;
}

bool
crawling_robot_interfaces__msg__LaserCorrectionStatus__copy(
  const crawling_robot_interfaces__msg__LaserCorrectionStatus * input,
  crawling_robot_interfaces__msg__LaserCorrectionStatus * output)
{
  if (!input || !output) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__copy(
      &(input->header), &(output->header)))
  {
    return false;
  }
  // active
  output->active = input->active;
  // contour_valid
  output->contour_valid = input->contour_valid;
  // geometry_valid
  output->geometry_valid = input->geometry_valid;
  // lateral_error_m
  output->lateral_error_m = input->lateral_error_m;
  // preview_lateral_error_m
  output->preview_lateral_error_m = input->preview_lateral_error_m;
  // heading_error_rad
  output->heading_error_rad = input->heading_error_rad;
  // curvature_1pm
  output->curvature_1pm = input->curvature_1pm;
  // angular_command_rad_s
  output->angular_command_rad_s = input->angular_command_rad_s;
  // angular_accel_rad_s2
  output->angular_accel_rad_s2 = input->angular_accel_rad_s2;
  // linear_command_m_s
  output->linear_command_m_s = input->linear_command_m_s;
  // contour_lateral_m
  output->contour_lateral_m = input->contour_lateral_m;
  // confidence
  output->confidence = input->confidence;
  // fit_residual_m
  output->fit_residual_m = input->fit_residual_m;
  // trajectory_points
  output->trajectory_points = input->trajectory_points;
  return true;
}

crawling_robot_interfaces__msg__LaserCorrectionStatus *
crawling_robot_interfaces__msg__LaserCorrectionStatus__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__msg__LaserCorrectionStatus * msg = (crawling_robot_interfaces__msg__LaserCorrectionStatus *)allocator.allocate(sizeof(crawling_robot_interfaces__msg__LaserCorrectionStatus), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(crawling_robot_interfaces__msg__LaserCorrectionStatus));
  bool success = crawling_robot_interfaces__msg__LaserCorrectionStatus__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
crawling_robot_interfaces__msg__LaserCorrectionStatus__destroy(crawling_robot_interfaces__msg__LaserCorrectionStatus * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    crawling_robot_interfaces__msg__LaserCorrectionStatus__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__init(crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__msg__LaserCorrectionStatus * data = NULL;

  if (size) {
    data = (crawling_robot_interfaces__msg__LaserCorrectionStatus *)allocator.zero_allocate(size, sizeof(crawling_robot_interfaces__msg__LaserCorrectionStatus), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = crawling_robot_interfaces__msg__LaserCorrectionStatus__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        crawling_robot_interfaces__msg__LaserCorrectionStatus__fini(&data[i - 1]);
      }
      allocator.deallocate(data, allocator.state);
      return false;
    }
  }
  array->data = data;
  array->size = size;
  array->capacity = size;
  return true;
}

void
crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__fini(crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * array)
{
  if (!array) {
    return;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  if (array->data) {
    // ensure that data and capacity values are consistent
    assert(array->capacity > 0);
    // finalize all array elements
    for (size_t i = 0; i < array->capacity; ++i) {
      crawling_robot_interfaces__msg__LaserCorrectionStatus__fini(&array->data[i]);
    }
    allocator.deallocate(array->data, allocator.state);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
  } else {
    // ensure that data, size, and capacity values are consistent
    assert(0 == array->size);
    assert(0 == array->capacity);
  }
}

crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence *
crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * array = (crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence *)allocator.allocate(sizeof(crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__destroy(crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__are_equal(const crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * lhs, const crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!crawling_robot_interfaces__msg__LaserCorrectionStatus__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence__copy(
  const crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * input,
  crawling_robot_interfaces__msg__LaserCorrectionStatus__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(crawling_robot_interfaces__msg__LaserCorrectionStatus);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    crawling_robot_interfaces__msg__LaserCorrectionStatus * data =
      (crawling_robot_interfaces__msg__LaserCorrectionStatus *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!crawling_robot_interfaces__msg__LaserCorrectionStatus__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          crawling_robot_interfaces__msg__LaserCorrectionStatus__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!crawling_robot_interfaces__msg__LaserCorrectionStatus__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
