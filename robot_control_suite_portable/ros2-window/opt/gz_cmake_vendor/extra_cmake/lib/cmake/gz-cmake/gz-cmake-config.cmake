cmake_minimum_required(VERSION 3.10.2 FATAL_ERROR)

find_package(gz-cmake3 ${gz-cmake_FIND_VERSION} REQUIRED COMPONENTS ${gz-cmake_FIND_COMPONENTS})

# Set up the core library and add it to the list of all components
add_library(gz-cmake::gz-cmake ALIAS gz-cmake3::gz-cmake3)
add_library(gz-cmake::core ALIAS gz-cmake3::gz-cmake3)

# Retrieve the list of components
get_target_property(components gz-cmake3::requested INTERFACE_LINK_LIBRARIES)

foreach(component ${components})
  # Skip the core library
  if(${component} STREQUAL gz-cmake3::gz-cmake3)
    continue()
  endif()

  # Change "gz-libN::gz-libN-component" to "component"
  string(REGEX REPLACE "gz-cmake3::gz-cmake3-" "" component_name ${component})
  add_library(gz-cmake::${component_name} ALIAS ${component})
endforeach()

# Add a root gz-lib alias
add_library(gz-cmake ALIAS gz-cmake3::gz-cmake3)
