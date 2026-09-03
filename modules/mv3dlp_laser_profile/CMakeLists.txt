cmake_minimum_required(VERSION 3.16)

project(mv3dlp_laser_profile VERSION 0.1.0 LANGUAGES CXX)

option(MV3DLP_BUILD_EXAMPLES "Build example applications" ON)

add_library(mv3dlp_laser_profile
    src/driver.cpp
    src/vendor_sdk.cpp
)

add_library(mv3dlp::laser_profile ALIAS mv3dlp_laser_profile)

target_compile_features(mv3dlp_laser_profile PUBLIC cxx_std_17)

target_include_directories(mv3dlp_laser_profile
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(WIN32)
    target_compile_definitions(mv3dlp_laser_profile PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(mv3dlp_laser_profile PRIVATE dl pthread)
endif()

if(MV3DLP_BUILD_EXAMPLES)
    add_executable(mv3dlp_fetch_frame examples/fetch_frame.cpp)
    target_link_libraries(mv3dlp_fetch_frame PRIVATE mv3dlp_laser_profile)
endif()

install(TARGETS mv3dlp_laser_profile
    EXPORT mv3dlp_laser_profileTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT mv3dlp_laser_profileTargets
    FILE mv3dlp_laser_profileTargets.cmake
    NAMESPACE mv3dlp::
    DESTINATION lib/cmake/mv3dlp_laser_profile
)
