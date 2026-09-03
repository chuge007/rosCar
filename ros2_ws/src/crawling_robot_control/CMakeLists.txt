cmake_minimum_required(VERSION 3.20)
project(crawling_robot_control)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(crawling_robot_interfaces REQUIRED)
find_package(crawling_robot_logging REQUIRED)

add_executable(laser_path_follower_node src/laser_path_follower_node.cpp)
target_compile_features(laser_path_follower_node PRIVATE cxx_std_17)
ament_target_dependencies(laser_path_follower_node
  rclcpp geometry_msgs sensor_msgs nav_msgs std_srvs crawling_robot_interfaces crawling_robot_logging
)

install(TARGETS laser_path_follower_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

ament_package()
