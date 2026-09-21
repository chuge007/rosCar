#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gz-math7::gz-math7" for configuration "RelWithDebInfo"
set_property(TARGET gz-math7::gz-math7 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(gz-math7::gz-math7 PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/gz-math7.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/gz-math7.dll"
  )

list(APPEND _cmake_import_check_targets gz-math7::gz-math7 )
list(APPEND _cmake_import_check_files_for_gz-math7::gz-math7 "${_IMPORT_PREFIX}/lib/gz-math7.lib" "${_IMPORT_PREFIX}/bin/gz-math7.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
