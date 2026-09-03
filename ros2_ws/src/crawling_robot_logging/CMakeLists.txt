cmake_minimum_required(VERSION 3.20)
project(crawling_robot_logging)

find_package(ament_cmake REQUIRED)

install(DIRECTORY include/
  DESTINATION include
)

ament_export_include_directories(include)
ament_package()
