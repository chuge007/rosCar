cmake_minimum_required(VERSION 3.20)
project(crawling_robot_drivers)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(crawling_robot_interfaces REQUIRED)
find_package(crawling_robot_logging REQUIRED)

add_library(crawling_robot_protocols
  src/rim302_protocol.cpp
  src/servo_v38.cpp
  src/win32_serial_port.cpp
)
target_compile_features(crawling_robot_protocols PUBLIC cxx_std_17)
target_include_directories(crawling_robot_protocols PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_definitions(crawling_robot_protocols PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(base_drive_node src/base_drive_node.cpp)
target_link_libraries(base_drive_node crawling_robot_protocols)
ament_target_dependencies(base_drive_node
  rclcpp geometry_msgs sensor_msgs std_srvs crawling_robot_interfaces crawling_robot_logging
)

add_executable(rim302_imu_node src/rim302_imu_node.cpp)
target_include_directories(rim302_imu_node PRIVATE include)
target_link_libraries(rim302_imu_node crawling_robot_protocols)
ament_target_dependencies(rim302_imu_node rclcpp geometry_msgs sensor_msgs crawling_robot_logging)

add_executable(encoder_odom_node src/encoder_odom_node.cpp)
ament_target_dependencies(encoder_odom_node
  rclcpp geometry_msgs nav_msgs sensor_msgs std_msgs tf2 tf2_geometry_msgs tf2_ros
  crawling_robot_logging
)

add_executable(modbus_encoder_node src/modbus_encoder_node.cpp)
target_link_libraries(modbus_encoder_node crawling_robot_protocols)
ament_target_dependencies(modbus_encoder_node rclcpp std_msgs crawling_robot_logging)

add_executable(slcan_can_bridge_node src/slcan_can_bridge_node.cpp)
target_link_libraries(slcan_can_bridge_node crawling_robot_protocols)
ament_target_dependencies(slcan_can_bridge_node rclcpp crawling_robot_interfaces crawling_robot_logging)

add_executable(linear_axis_node src/linear_axis_node.cpp)
ament_target_dependencies(linear_axis_node rclcpp sensor_msgs std_srvs trajectory_msgs crawling_robot_logging)

add_executable(canopen_axis_node src/canopen_axis_node.cpp)
target_include_directories(canopen_axis_node PRIVATE include)
ament_target_dependencies(canopen_axis_node
  rclcpp sensor_msgs std_srvs trajectory_msgs crawling_robot_interfaces crawling_robot_logging
)

install(TARGETS
  crawling_robot_protocols
  base_drive_node
  rim302_imu_node
  encoder_odom_node
  modbus_encoder_node
  slcan_can_bridge_node
  linear_axis_node
  canopen_axis_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_protocols test/test_protocols.cpp)
  if(TARGET test_protocols)
    target_link_libraries(test_protocols crawling_robot_protocols)
    target_include_directories(test_protocols PRIVATE include)
  endif()
endif()

ament_export_include_directories(include)
ament_package()
