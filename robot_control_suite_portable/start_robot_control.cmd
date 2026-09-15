@echo off
setlocal
set "APP_DIR=%~dp0"
set "ROS2_WINDOW_ROOT=%APP_DIR%ros2-window"
set "ROBOT_CONTROL_WS_INSTALL=%APP_DIR%ros2-overlay"
set "ROS_LOCALHOST_ONLY=1"
set "ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST"
start "Robot Control Suite" /D "%APP_DIR%" "%APP_DIR%robot_control_suite.exe"
