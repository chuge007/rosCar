// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from crawling_robot_interfaces:msg\LaserCorrectionStatus.idl
// generated code does not contain a copyright notice

#include "crawling_robot_interfaces/msg/detail/laser_correction_status__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_crawling_robot_interfaces
const rosidl_type_hash_t *
crawling_robot_interfaces__msg__LaserCorrectionStatus__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xe3, 0x47, 0x02, 0x67, 0x6e, 0xf8, 0x19, 0x2a,
      0x02, 0xad, 0xa0, 0xca, 0xa6, 0x76, 0xa8, 0xb3,
      0x07, 0x9f, 0x2f, 0x50, 0xae, 0xd2, 0xed, 0x36,
      0x52, 0x79, 0xa5, 0x10, 0xad, 0x25, 0x3e, 0xf3,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "std_msgs/msg/detail/header__functions.h"
#include "builtin_interfaces/msg/detail/time__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
static const rosidl_type_hash_t std_msgs__msg__Header__EXPECTED_HASH = {1, {
    0xf4, 0x9f, 0xb3, 0xae, 0x2c, 0xf0, 0x70, 0xf7,
    0x93, 0x64, 0x5f, 0xf7, 0x49, 0x68, 0x3a, 0xc6,
    0xb0, 0x62, 0x03, 0xe4, 0x1c, 0x89, 0x1e, 0x17,
    0x70, 0x1b, 0x1c, 0xb5, 0x97, 0xce, 0x6a, 0x01,
  }};
#endif

static char crawling_robot_interfaces__msg__LaserCorrectionStatus__TYPE_NAME[] = "crawling_robot_interfaces/msg/LaserCorrectionStatus";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";
static char std_msgs__msg__Header__TYPE_NAME[] = "std_msgs/msg/Header";

// Define type names, field names, and default values
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__header[] = "header";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__active[] = "active";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__contour_valid[] = "contour_valid";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__geometry_valid[] = "geometry_valid";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__lateral_error_m[] = "lateral_error_m";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__preview_lateral_error_m[] = "preview_lateral_error_m";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__heading_error_rad[] = "heading_error_rad";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__curvature_1pm[] = "curvature_1pm";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__angular_command_rad_s[] = "angular_command_rad_s";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__angular_accel_rad_s2[] = "angular_accel_rad_s2";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__linear_command_m_s[] = "linear_command_m_s";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__contour_lateral_m[] = "contour_lateral_m";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__confidence[] = "confidence";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__fit_residual_m[] = "fit_residual_m";
static char crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__trajectory_points[] = "trajectory_points";

static rosidl_runtime_c__type_description__Field crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELDS[] = {
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__header, 6, 6},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {std_msgs__msg__Header__TYPE_NAME, 19, 19},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__active, 6, 6},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__contour_valid, 13, 13},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__geometry_valid, 14, 14},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__lateral_error_m, 15, 15},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__preview_lateral_error_m, 23, 23},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__heading_error_rad, 17, 17},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__curvature_1pm, 13, 13},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__angular_command_rad_s, 21, 21},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__angular_accel_rad_s2, 20, 20},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__linear_command_m_s, 18, 18},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__contour_lateral_m, 17, 17},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__confidence, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__fit_residual_m, 14, 14},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELD_NAME__trajectory_points, 17, 17},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_UINT32,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription crawling_robot_interfaces__msg__LaserCorrectionStatus__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {std_msgs__msg__Header__TYPE_NAME, 19, 19},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
crawling_robot_interfaces__msg__LaserCorrectionStatus__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {crawling_robot_interfaces__msg__LaserCorrectionStatus__TYPE_NAME, 51, 51},
      {crawling_robot_interfaces__msg__LaserCorrectionStatus__FIELDS, 15, 15},
    },
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__REFERENCED_TYPE_DESCRIPTIONS, 2, 2},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&std_msgs__msg__Header__EXPECTED_HASH, std_msgs__msg__Header__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[1].fields = std_msgs__msg__Header__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "std_msgs/Header header\n"
  "bool active\n"
  "bool contour_valid\n"
  "bool geometry_valid\n"
  "float64 lateral_error_m\n"
  "float64 preview_lateral_error_m\n"
  "float64 heading_error_rad\n"
  "float64 curvature_1pm\n"
  "float64 angular_command_rad_s\n"
  "float64 angular_accel_rad_s2\n"
  "float64 linear_command_m_s\n"
  "float64 contour_lateral_m\n"
  "float64 confidence\n"
  "float64 fit_residual_m\n"
  "uint32 trajectory_points";

static char msg_encoding[] = "msg";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
crawling_robot_interfaces__msg__LaserCorrectionStatus__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {crawling_robot_interfaces__msg__LaserCorrectionStatus__TYPE_NAME, 51, 51},
    {msg_encoding, 3, 3},
    {toplevel_type_raw_source, 357, 357},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
crawling_robot_interfaces__msg__LaserCorrectionStatus__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[3];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 3, 3};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *crawling_robot_interfaces__msg__LaserCorrectionStatus__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *std_msgs__msg__Header__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
