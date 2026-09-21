#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "mv3dlp::mv3dlp_laser_profile" for configuration "Release"
set_property(TARGET mv3dlp::mv3dlp_laser_profile APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(mv3dlp::mv3dlp_laser_profile PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/mv3dlp_laser_profile.lib"
  )

list(APPEND _cmake_import_check_targets mv3dlp::mv3dlp_laser_profile )
list(APPEND _cmake_import_check_files_for_mv3dlp::mv3dlp_laser_profile "${_IMPORT_PREFIX}/lib/mv3dlp_laser_profile.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
