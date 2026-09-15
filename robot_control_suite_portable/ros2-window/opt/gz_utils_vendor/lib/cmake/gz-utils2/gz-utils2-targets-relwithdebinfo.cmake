#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gz-utils2::gz-utils2" for configuration "RelWithDebInfo"
set_property(TARGET gz-utils2::gz-utils2 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(gz-utils2::gz-utils2 PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/gz-utils2.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/gz-utils2.dll"
  )

list(APPEND _cmake_import_check_targets gz-utils2::gz-utils2 )
list(APPEND _cmake_import_check_files_for_gz-utils2::gz-utils2 "${_IMPORT_PREFIX}/lib/gz-utils2.lib" "${_IMPORT_PREFIX}/bin/gz-utils2.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
