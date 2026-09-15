// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from crawling_robot_interfaces:srv\SetLocalizationReference.idl
// generated code does not contain a copyright notice

#include "crawling_robot_interfaces/srv/detail/set_localization_reference__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_crawling_robot_interfaces
const rosidl_type_hash_t *
crawling_robot_interfaces__srv__SetLocalizationReference__get_type_hash(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x37, 0x37, 0x46, 0x92, 0xf7, 0xa0, 0x39, 0x8b,
      0x66, 0xcf, 0x64, 0x22, 0x33, 0x59, 0x61, 0xfa,
      0x0d, 0x3e, 0xe6, 0x9c, 0x2a, 0x45, 0x46, 0xac,
      0x48, 0xef, 0x6f, 0xc9, 0x79, 0xdd, 0xf7, 0x45,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_crawling_robot_interfaces
const rosidl_type_hash_t *
crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x88, 0x38, 0x01, 0x15, 0x40, 0xe9, 0x90, 0xcb,
      0x07, 0x2d, 0xc5, 0x1c, 0x94, 0x55, 0x01, 0x6e,
      0x06, 0x50, 0x4d, 0xc4, 0x3f, 0x73, 0xac, 0x39,
      0xe0, 0x74, 0xa2, 0xff, 0x86, 0x15, 0x3d, 0x03,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_crawling_robot_interfaces
const rosidl_type_hash_t *
crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x27, 0x68, 0xe0, 0x4a, 0xe7, 0x64, 0xe9, 0x9f,
      0xfa, 0xb3, 0x69, 0x2e, 0x32, 0x3a, 0x72, 0x2c,
      0xfc, 0xf2, 0x58, 0x7a, 0x32, 0x93, 0x0d, 0xb9,
      0x01, 0x2b, 0x4b, 0x7a, 0x07, 0x20, 0xb7, 0xda,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_crawling_robot_interfaces
const rosidl_type_hash_t *
crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xe7, 0xf2, 0x60, 0xcf, 0xd0, 0x20, 0x76, 0xfd,
      0x51, 0xa9, 0x97, 0xfc, 0x3e, 0xe9, 0x60, 0xfe,
      0x94, 0x55, 0x84, 0x24, 0x32, 0x2a, 0xa8, 0x51,
      0xc3, 0xfc, 0x66, 0xf6, 0x6c, 0xc3, 0xf9, 0x17,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "service_msgs/msg/detail/service_event_info__functions.h"
#include "builtin_interfaces/msg/detail/time__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
static const rosidl_type_hash_t service_msgs__msg__ServiceEventInfo__EXPECTED_HASH = {1, {
    0x41, 0xbc, 0xbb, 0xe0, 0x7a, 0x75, 0xc9, 0xb5,
    0x2b, 0xc9, 0x6b, 0xfd, 0x5c, 0x24, 0xd7, 0xf0,
    0xfc, 0x0a, 0x08, 0xc0, 0xcb, 0x79, 0x21, 0xb3,
    0x37, 0x3c, 0x57, 0x32, 0x34, 0x5a, 0x6f, 0x45,
  }};
#endif

static char crawling_robot_interfaces__srv__SetLocalizationReference__TYPE_NAME[] = "crawling_robot_interfaces/srv/SetLocalizationReference";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Event__TYPE_NAME[] = "crawling_robot_interfaces/srv/SetLocalizationReference_Event";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME[] = "crawling_robot_interfaces/srv/SetLocalizationReference_Request";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME[] = "crawling_robot_interfaces/srv/SetLocalizationReference_Response";
static char service_msgs__msg__ServiceEventInfo__TYPE_NAME[] = "service_msgs/msg/ServiceEventInfo";

// Define type names, field names, and default values
static char crawling_robot_interfaces__srv__SetLocalizationReference__FIELD_NAME__request_message[] = "request_message";
static char crawling_robot_interfaces__srv__SetLocalizationReference__FIELD_NAME__response_message[] = "response_message";
static char crawling_robot_interfaces__srv__SetLocalizationReference__FIELD_NAME__event_message[] = "event_message";

static rosidl_runtime_c__type_description__Field crawling_robot_interfaces__srv__SetLocalizationReference__FIELDS[] = {
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference__FIELD_NAME__request_message, 15, 15},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME, 62, 62},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference__FIELD_NAME__response_message, 16, 16},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME, 63, 63},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference__FIELD_NAME__event_message, 13, 13},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {crawling_robot_interfaces__srv__SetLocalizationReference_Event__TYPE_NAME, 60, 60},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription crawling_robot_interfaces__srv__SetLocalizationReference__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Event__TYPE_NAME, 60, 60},
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME, 62, 62},
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME, 63, 63},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
crawling_robot_interfaces__srv__SetLocalizationReference__get_type_description(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {crawling_robot_interfaces__srv__SetLocalizationReference__TYPE_NAME, 54, 54},
      {crawling_robot_interfaces__srv__SetLocalizationReference__FIELDS, 3, 3},
    },
    {crawling_robot_interfaces__srv__SetLocalizationReference__REFERENCED_TYPE_DESCRIPTIONS, 5, 5},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[3].fields = crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[4].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char crawling_robot_interfaces__srv__SetLocalizationReference_Request__FIELD_NAME__contour_lateral_m[] = "contour_lateral_m";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Request__FIELD_NAME__heading_reference_rad[] = "heading_reference_rad";

static rosidl_runtime_c__type_description__Field crawling_robot_interfaces__srv__SetLocalizationReference_Request__FIELDS[] = {
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Request__FIELD_NAME__contour_lateral_m, 17, 17},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Request__FIELD_NAME__heading_reference_rad, 21, 21},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME, 62, 62},
      {crawling_robot_interfaces__srv__SetLocalizationReference_Request__FIELDS, 2, 2},
    },
    {NULL, 0, 0},
  };
  if (!constructed) {
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char crawling_robot_interfaces__srv__SetLocalizationReference_Response__FIELD_NAME__success[] = "success";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Response__FIELD_NAME__message[] = "message";

static rosidl_runtime_c__type_description__Field crawling_robot_interfaces__srv__SetLocalizationReference_Response__FIELDS[] = {
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Response__FIELD_NAME__success, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Response__FIELD_NAME__message, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME, 63, 63},
      {crawling_robot_interfaces__srv__SetLocalizationReference_Response__FIELDS, 2, 2},
    },
    {NULL, 0, 0},
  };
  if (!constructed) {
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELD_NAME__info[] = "info";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELD_NAME__request[] = "request";
static char crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELD_NAME__response[] = "response";

static rosidl_runtime_c__type_description__Field crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELDS[] = {
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELD_NAME__info, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELD_NAME__request, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME, 62, 62},
    },
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELD_NAME__response, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME, 63, 63},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription crawling_robot_interfaces__srv__SetLocalizationReference_Event__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME, 62, 62},
    {NULL, 0, 0},
  },
  {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME, 63, 63},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {crawling_robot_interfaces__srv__SetLocalizationReference_Event__TYPE_NAME, 60, 60},
      {crawling_robot_interfaces__srv__SetLocalizationReference_Event__FIELDS, 3, 3},
    },
    {crawling_robot_interfaces__srv__SetLocalizationReference_Event__REFERENCED_TYPE_DESCRIPTIONS, 4, 4},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[3].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "float64 contour_lateral_m\n"
  "float64 heading_reference_rad\n"
  "---\n"
  "bool success\n"
  "string message";

static char srv_encoding[] = "srv";
static char implicit_encoding[] = "implicit";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
crawling_robot_interfaces__srv__SetLocalizationReference__get_individual_type_description_source(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {crawling_robot_interfaces__srv__SetLocalizationReference__TYPE_NAME, 54, 54},
    {srv_encoding, 3, 3},
    {toplevel_type_raw_source, 88, 88},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Request__TYPE_NAME, 62, 62},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Response__TYPE_NAME, 63, 63},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {crawling_robot_interfaces__srv__SetLocalizationReference_Event__TYPE_NAME, 60, 60},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
crawling_robot_interfaces__srv__SetLocalizationReference__get_type_description_sources(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[6];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 6, 6};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *crawling_robot_interfaces__srv__SetLocalizationReference__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_individual_type_description_source(NULL);
    sources[3] = *crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_individual_type_description_source(NULL);
    sources[4] = *crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_individual_type_description_source(NULL);
    sources[5] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[1];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 1, 1};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_individual_type_description_source(NULL),
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[1];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 1, 1};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_individual_type_description_source(NULL),
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[5];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 5, 5};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *crawling_robot_interfaces__srv__SetLocalizationReference_Event__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *crawling_robot_interfaces__srv__SetLocalizationReference_Request__get_individual_type_description_source(NULL);
    sources[3] = *crawling_robot_interfaces__srv__SetLocalizationReference_Response__get_individual_type_description_source(NULL);
    sources[4] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
