// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from crawling_robot_interfaces:srv\SetDriveLimits.idl
// generated code does not contain a copyright notice
#include "crawling_robot_interfaces/srv/detail/set_drive_limits__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"

bool
crawling_robot_interfaces__srv__SetDriveLimits_Request__init(crawling_robot_interfaces__srv__SetDriveLimits_Request * msg)
{
  if (!msg) {
    return false;
  }
  // max_linear_speed_m_s
  // max_angular_speed_rad_s
  // max_linear_accel_m_s2
  // max_angular_accel_rad_s2
  // max_wheel_speed_m_s
  // minimum_inner_wheel_ratio
  return true;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Request__fini(crawling_robot_interfaces__srv__SetDriveLimits_Request * msg)
{
  if (!msg) {
    return;
  }
  // max_linear_speed_m_s
  // max_angular_speed_rad_s
  // max_linear_accel_m_s2
  // max_angular_accel_rad_s2
  // max_wheel_speed_m_s
  // minimum_inner_wheel_ratio
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Request__are_equal(const crawling_robot_interfaces__srv__SetDriveLimits_Request * lhs, const crawling_robot_interfaces__srv__SetDriveLimits_Request * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // max_linear_speed_m_s
  if (lhs->max_linear_speed_m_s != rhs->max_linear_speed_m_s) {
    return false;
  }
  // max_angular_speed_rad_s
  if (lhs->max_angular_speed_rad_s != rhs->max_angular_speed_rad_s) {
    return false;
  }
  // max_linear_accel_m_s2
  if (lhs->max_linear_accel_m_s2 != rhs->max_linear_accel_m_s2) {
    return false;
  }
  // max_angular_accel_rad_s2
  if (lhs->max_angular_accel_rad_s2 != rhs->max_angular_accel_rad_s2) {
    return false;
  }
  // max_wheel_speed_m_s
  if (lhs->max_wheel_speed_m_s != rhs->max_wheel_speed_m_s) {
    return false;
  }
  // minimum_inner_wheel_ratio
  if (lhs->minimum_inner_wheel_ratio != rhs->minimum_inner_wheel_ratio) {
    return false;
  }
  return true;
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Request__copy(
  const crawling_robot_interfaces__srv__SetDriveLimits_Request * input,
  crawling_robot_interfaces__srv__SetDriveLimits_Request * output)
{
  if (!input || !output) {
    return false;
  }
  // max_linear_speed_m_s
  output->max_linear_speed_m_s = input->max_linear_speed_m_s;
  // max_angular_speed_rad_s
  output->max_angular_speed_rad_s = input->max_angular_speed_rad_s;
  // max_linear_accel_m_s2
  output->max_linear_accel_m_s2 = input->max_linear_accel_m_s2;
  // max_angular_accel_rad_s2
  output->max_angular_accel_rad_s2 = input->max_angular_accel_rad_s2;
  // max_wheel_speed_m_s
  output->max_wheel_speed_m_s = input->max_wheel_speed_m_s;
  // minimum_inner_wheel_ratio
  output->minimum_inner_wheel_ratio = input->minimum_inner_wheel_ratio;
  return true;
}

crawling_robot_interfaces__srv__SetDriveLimits_Request *
crawling_robot_interfaces__srv__SetDriveLimits_Request__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Request * msg = (crawling_robot_interfaces__srv__SetDriveLimits_Request *)allocator.allocate(sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Request), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Request));
  bool success = crawling_robot_interfaces__srv__SetDriveLimits_Request__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Request__destroy(crawling_robot_interfaces__srv__SetDriveLimits_Request * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    crawling_robot_interfaces__srv__SetDriveLimits_Request__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__init(crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Request * data = NULL;

  if (size) {
    data = (crawling_robot_interfaces__srv__SetDriveLimits_Request *)allocator.zero_allocate(size, sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Request), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = crawling_robot_interfaces__srv__SetDriveLimits_Request__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        crawling_robot_interfaces__srv__SetDriveLimits_Request__fini(&data[i - 1]);
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
crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__fini(crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * array)
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
      crawling_robot_interfaces__srv__SetDriveLimits_Request__fini(&array->data[i]);
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

crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence *
crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * array = (crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence *)allocator.allocate(sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__destroy(crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__are_equal(const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * lhs, const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__copy(
  const crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * input,
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Request);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    crawling_robot_interfaces__srv__SetDriveLimits_Request * data =
      (crawling_robot_interfaces__srv__SetDriveLimits_Request *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          crawling_robot_interfaces__srv__SetDriveLimits_Request__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `message`
#include "rosidl_runtime_c/string_functions.h"

bool
crawling_robot_interfaces__srv__SetDriveLimits_Response__init(crawling_robot_interfaces__srv__SetDriveLimits_Response * msg)
{
  if (!msg) {
    return false;
  }
  // success
  // message
  if (!rosidl_runtime_c__String__init(&msg->message)) {
    crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(msg);
    return false;
  }
  return true;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(crawling_robot_interfaces__srv__SetDriveLimits_Response * msg)
{
  if (!msg) {
    return;
  }
  // success
  // message
  rosidl_runtime_c__String__fini(&msg->message);
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Response__are_equal(const crawling_robot_interfaces__srv__SetDriveLimits_Response * lhs, const crawling_robot_interfaces__srv__SetDriveLimits_Response * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // success
  if (lhs->success != rhs->success) {
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->message), &(rhs->message)))
  {
    return false;
  }
  return true;
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Response__copy(
  const crawling_robot_interfaces__srv__SetDriveLimits_Response * input,
  crawling_robot_interfaces__srv__SetDriveLimits_Response * output)
{
  if (!input || !output) {
    return false;
  }
  // success
  output->success = input->success;
  // message
  if (!rosidl_runtime_c__String__copy(
      &(input->message), &(output->message)))
  {
    return false;
  }
  return true;
}

crawling_robot_interfaces__srv__SetDriveLimits_Response *
crawling_robot_interfaces__srv__SetDriveLimits_Response__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Response * msg = (crawling_robot_interfaces__srv__SetDriveLimits_Response *)allocator.allocate(sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Response), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Response));
  bool success = crawling_robot_interfaces__srv__SetDriveLimits_Response__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Response__destroy(crawling_robot_interfaces__srv__SetDriveLimits_Response * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__init(crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Response * data = NULL;

  if (size) {
    data = (crawling_robot_interfaces__srv__SetDriveLimits_Response *)allocator.zero_allocate(size, sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Response), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = crawling_robot_interfaces__srv__SetDriveLimits_Response__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(&data[i - 1]);
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
crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__fini(crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * array)
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
      crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(&array->data[i]);
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

crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence *
crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * array = (crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence *)allocator.allocate(sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__destroy(crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__are_equal(const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * lhs, const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__copy(
  const crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * input,
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Response);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    crawling_robot_interfaces__srv__SetDriveLimits_Response * data =
      (crawling_robot_interfaces__srv__SetDriveLimits_Response *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          crawling_robot_interfaces__srv__SetDriveLimits_Response__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `info`
#include "service_msgs/msg/detail/service_event_info__functions.h"
// Member `request`
// Member `response`
// already included above
// #include "crawling_robot_interfaces/srv/detail/set_drive_limits__functions.h"

bool
crawling_robot_interfaces__srv__SetDriveLimits_Event__init(crawling_robot_interfaces__srv__SetDriveLimits_Event * msg)
{
  if (!msg) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__init(&msg->info)) {
    crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(msg);
    return false;
  }
  // request
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__init(&msg->request, 0)) {
    crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(msg);
    return false;
  }
  // response
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__init(&msg->response, 0)) {
    crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(msg);
    return false;
  }
  return true;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(crawling_robot_interfaces__srv__SetDriveLimits_Event * msg)
{
  if (!msg) {
    return;
  }
  // info
  service_msgs__msg__ServiceEventInfo__fini(&msg->info);
  // request
  crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__fini(&msg->request);
  // response
  crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__fini(&msg->response);
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Event__are_equal(const crawling_robot_interfaces__srv__SetDriveLimits_Event * lhs, const crawling_robot_interfaces__srv__SetDriveLimits_Event * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__are_equal(
      &(lhs->info), &(rhs->info)))
  {
    return false;
  }
  // request
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__are_equal(
      &(lhs->request), &(rhs->request)))
  {
    return false;
  }
  // response
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__are_equal(
      &(lhs->response), &(rhs->response)))
  {
    return false;
  }
  return true;
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Event__copy(
  const crawling_robot_interfaces__srv__SetDriveLimits_Event * input,
  crawling_robot_interfaces__srv__SetDriveLimits_Event * output)
{
  if (!input || !output) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__copy(
      &(input->info), &(output->info)))
  {
    return false;
  }
  // request
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Request__Sequence__copy(
      &(input->request), &(output->request)))
  {
    return false;
  }
  // response
  if (!crawling_robot_interfaces__srv__SetDriveLimits_Response__Sequence__copy(
      &(input->response), &(output->response)))
  {
    return false;
  }
  return true;
}

crawling_robot_interfaces__srv__SetDriveLimits_Event *
crawling_robot_interfaces__srv__SetDriveLimits_Event__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Event * msg = (crawling_robot_interfaces__srv__SetDriveLimits_Event *)allocator.allocate(sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Event), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Event));
  bool success = crawling_robot_interfaces__srv__SetDriveLimits_Event__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Event__destroy(crawling_robot_interfaces__srv__SetDriveLimits_Event * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__init(crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Event * data = NULL;

  if (size) {
    data = (crawling_robot_interfaces__srv__SetDriveLimits_Event *)allocator.zero_allocate(size, sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Event), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = crawling_robot_interfaces__srv__SetDriveLimits_Event__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(&data[i - 1]);
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
crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__fini(crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * array)
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
      crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(&array->data[i]);
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

crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence *
crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * array = (crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence *)allocator.allocate(sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__destroy(crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__are_equal(const crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * lhs, const crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!crawling_robot_interfaces__srv__SetDriveLimits_Event__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence__copy(
  const crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * input,
  crawling_robot_interfaces__srv__SetDriveLimits_Event__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(crawling_robot_interfaces__srv__SetDriveLimits_Event);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    crawling_robot_interfaces__srv__SetDriveLimits_Event * data =
      (crawling_robot_interfaces__srv__SetDriveLimits_Event *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!crawling_robot_interfaces__srv__SetDriveLimits_Event__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          crawling_robot_interfaces__srv__SetDriveLimits_Event__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!crawling_robot_interfaces__srv__SetDriveLimits_Event__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
