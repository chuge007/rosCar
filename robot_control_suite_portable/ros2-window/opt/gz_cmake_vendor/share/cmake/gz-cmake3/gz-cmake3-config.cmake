# - Config file for gz-cmake (not for any other Gazebo libraries).
#
# To find and load gz-cmake modules into your project, simply invoke:
#
# find_package(gz-cmake3)
#
# That will define the variable gz-cmake3_FOUND, and will open up access
# to all the cmake-modules and find-modules that are provided by gz-cmake.

# We explicitly set the desired cmake version to ensure that the policy settings
# of users or of toolchains do not result in the wrong behavior for our modules.
# Note that the call to find_package(~) will PUSH a new policy stack before
# taking on these version settings, and then that stack will POP after the
# find_package(~) has exited, so this will not affect the cmake policy settings
# of a caller.
cmake_minimum_required(VERSION 3.10.2 FATAL_ERROR)

if(gz-cmake3_CONFIG_INCLUDED)
  return()
endif()
set(gz-cmake3_CONFIG_INCLUDED TRUE)


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was gz-cmake-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

####################################################################################

if(NOT TARGET gz-cmake3::gz-cmake3-utilities)
  include("${CMAKE_CURRENT_LIST_DIR}/gz-cmake3-utilities-targets.cmake")

  # Create a simplified imported target name for the utilities library
  add_library(gz-cmake3::utilities INTERFACE IMPORTED)
  set_target_properties(gz-cmake3::utilities PROPERTIES
    INTERFACE_LINK_LIBRARIES gz-cmake3::gz-cmake3-utilities)

endif()

# We should only perform these operations if either
#  1. gz-cmake has not been included yet, or
#  2. we need a higher version of gz-cmake than the last one that was included
#
# The only time we should need to switch to a higher version of gz-cmake is
# when importing targets for dependencies, i.e. using the find-modules of
# gz-cmake. All find-modules that are available in older versions of gz-cmake
# should be available in newer versions of gz-cmake, so this should not create
# any conflicts.
if( NOT GZ_CMAKE_VERSION_MAJOR
    OR (GZ_CMAKE_VERSION_MAJOR LESS 3) )

  #--------------------------------------
  # Create a variable to indicate what version of gz-cmake we are using.
  # This variable does not follow the usual cmake naming convention because it
  # is a non-standard package variable.
  set(GZ_CMAKE_VERSION_MAJOR 3)
  set(IGNITION_CMAKE_VERSION_MAJOR ${GZ_CMAKE_VERSION_MAJOR})  # TODO(CH3): Deprecated. Remove on tock.

  #--------------------------------------
  # Initialize the GZ_CMAKE_DIR variable with the location of the cmake
  # directory that sits next to this find-module.
  set(GZ_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake3")

  #--------------------------------------
  # Add the location of this package's cmake directory to the CMAKE_MODULE_PATH
  list(APPEND CMAKE_MODULE_PATH "${GZ_CMAKE_DIR}")

  #--------------------------------------
  # include the master GzCMake module
  include(GzCMake)

  set(GZ_CMAKE_DOXYGEN_DIR "${PACKAGE_PREFIX_DIR}/share/gz/gz-cmake3/doxygen")
  set(GZ_CMAKE_CODECHECK_DIR "${PACKAGE_PREFIX_DIR}/share/gz/gz-cmake3/codecheck")
  set(GZ_CMAKE_BENCHMARK_DIR "${PACKAGE_PREFIX_DIR}/share/gz/gz-cmake3/benchmark")
  set(GZ_CMAKE_TOOLS_DIR "${PACKAGE_PREFIX_DIR}/share/gz/gz-cmake3/tools")

  set(IGNITION_CMAKE_DOXYGEN_DIR ${GZ_CMAKE_DOXYGEN_DIR})  # TODO(CH3): Deprecated. Remove on tock.
  set(IGNITION_CMAKE_CODECHECK_DIR ${GZ_CMAKE_CODECHECK_DIR})  # TODO(CH3): Deprecated. Remove on tock.
  set(IGNITION_CMAKE_BENCHMARK_DIR ${GZ_CMAKE_BENCHMARK_DIR})  # TODO(CH3): Deprecated. Remove on tock.
  set(IGNITION_CMAKE_TOOLS_DIR ${GZ_CMAKE_TOOLS_DIR})  # TODO(CH3): Deprecated. Remove on tock.

endif()
