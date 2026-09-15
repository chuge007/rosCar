@echo off
setlocal EnableExtensions
set "RELEASE_DIR=%~dp0"
set "RUNTIME_DIR=%RELEASE_DIR%runtime"
set "PYTHON_DIR=%RUNTIME_DIR%\python"
set "COLCON_PYTHON_EXECUTABLE=%PYTHON_DIR%\python.exe"
set "PATH=%RUNTIME_DIR%;%PYTHON_DIR%;%PYTHON_DIR%\Scripts;%PYTHON_DIR%\Library\bin;%RUNTIME_DIR%\bin;%RUNTIME_DIR%\Library\bin;%PATH%"
if not defined ROS_DOMAIN_ID set "ROS_DOMAIN_ID=0"
if not defined RMW_IMPLEMENTATION set "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"

rem Directly opening this file used to create a monitor with no base-drive
rem node behind it. Start the complete ROS stack once unless run_robot.ps1
rem already launched it and marked this child as part of that stack.
if /I not "%CRAWLING_ROBOT_STACK_LAUNCHER%"=="1" (
  start "Crawling Robot ROS" /D "%RELEASE_DIR%" powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RELEASE_DIR%run_robot.ps1" -SkipMonitor
  timeout /t 3 /nobreak >NUL
)
call "%RUNTIME_DIR%\local_setup.bat"
call "%RELEASE_DIR%install\local_setup.bat"
if "%~1"=="" (
  "%PYTHON_DIR%\python.exe" "%RELEASE_DIR%install\lib\crawling_robot_monitor\robot_monitor-script.py" --ros-args --params-file "%RELEASE_DIR%config\robot.yaml"
) else (
  "%PYTHON_DIR%\python.exe" "%RELEASE_DIR%install\lib\crawling_robot_monitor\robot_monitor-script.py" %*
)
exit /b %ERRORLEVEL%
