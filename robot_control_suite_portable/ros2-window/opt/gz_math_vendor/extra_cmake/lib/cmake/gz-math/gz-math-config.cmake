cmake_minimum_required(VERSION 3.10.2 FATAL_ERROR)

find_package(gz-math7 ${gz-math_FIND_VERSION} REQUIRED COMPONENTS ${gz-math_FIND_COMPONENTS})

# Set up the core library and add it to the list of all components
add_library(gz-math::gz-math ALIAS gz-math7::gz-math7)
add_library(gz-math::core ALIAS gz-math7::gz-math7)

# Retrieve the list of components
get_target_property(components gz-math7::requested INTERFACE_LINK_LIBRARIES)

foreach(component ${components})
  # Skip the core library
  if(${component} STREQUAL gz-math7::gz-math7)
    continue()
  endif()

  # Change "gz-libN::gz-libN-component" to "component"
  string(REGEX REPLACE "gz-math7::gz-math7-" "" component_name ${component})
  add_library(gz-math::${component_name} ALIAS ${component})
endforeach()

# Add a root gz-lib alias
add_library(gz-math ALIAS gz-math7::gz-math7)
