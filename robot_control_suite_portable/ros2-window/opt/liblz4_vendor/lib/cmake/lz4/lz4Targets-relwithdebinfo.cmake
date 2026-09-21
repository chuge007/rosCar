#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LZ4::lz4_shared" for configuration "RelWithDebInfo"
set_property(TARGET LZ4::lz4_shared APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(LZ4::lz4_shared PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/lz4.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/lz4.dll"
  )

list(APPEND _cmake_import_check_targets LZ4::lz4_shared )
list(APPEND _cmake_import_check_files_for_LZ4::lz4_shared "${_IMPORT_PREFIX}/lib/lz4.lib" "${_IMPORT_PREFIX}/bin/lz4.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
