#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "fastrtps" for configuration "RelWithDebInfo"
set_property(TARGET fastrtps APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(fastrtps PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/fastrtps-2.14.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "fastcdr;tinyxml2::tinyxml2"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/fastrtps-2.14.dll"
  )

list(APPEND _cmake_import_check_targets fastrtps )
list(APPEND _cmake_import_check_files_for_fastrtps "${_IMPORT_PREFIX}/lib/fastrtps-2.14.lib" "${_IMPORT_PREFIX}/bin/fastrtps-2.14.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
