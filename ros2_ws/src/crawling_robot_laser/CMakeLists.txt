cmake_minimum_required(VERSION 3.20)
project(crawling_robot_laser)

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(crawling_robot_interfaces REQUIRED)
find_package(crawling_robot_logging REQUIRED)

add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/mv3dlp_laser_profile
  ${CMAKE_CURRENT_BINARY_DIR}/mv3dlp_laser_profile
)

add_executable(mv3dlp_laser_node src/mv3dlp_laser_node.cpp)
target_compile_features(mv3dlp_laser_node PRIVATE cxx_std_17)
target_link_libraries(mv3dlp_laser_node mv3dlp_laser_profile)
ament_target_dependencies(mv3dlp_laser_node
  ament_index_cpp rclcpp sensor_msgs std_msgs crawling_robot_interfaces crawling_robot_logging
)

install(TARGETS mv3dlp_laser_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/mv3dlp_laser_profile/windows_x64/bin/
  DESTINATION lib/${PROJECT_NAME}/mv3dlp_sdk
)

ament_package()
