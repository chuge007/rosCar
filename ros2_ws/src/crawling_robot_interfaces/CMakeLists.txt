cmake_minimum_required(VERSION 3.20)
project(crawling_robot_interfaces)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/CanFrame.msg"
  "msg/DriveCommandStatus.msg"
  "msg/LaserProfile.msg"
  "msg/LaserCorrectionStatus.msg"
  "srv/SetDriveLimits.srv"
  "srv/SetMotorMapping.srv"
  "srv/SetLocalizationReference.srv"
  DEPENDENCIES std_msgs sensor_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
