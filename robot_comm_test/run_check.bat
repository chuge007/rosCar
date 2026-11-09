@echo off
chcp 65001 >nul
setlocal

rem Usage: run_check.bat [network|usb|imu] [hold_seconds]
rem   network : cameraSource0/1 from the ini (RTSP)
rem   usb     : probe USB camera indices 0,1 instead
rem   imu     : network cameras + IMU from the ini

set "MODE=%~1"
if "%MODE%"=="" set "MODE=network"
set "HOLD=%~2"
if "%HOLD%"=="" set "HOLD=40"

set "ROOT=%~dp0"
set "INI=%ROOT%..\startup_deploy\robot_host\app\config\drive_settings.ini"

cd /d "%ROOT%release"
set PATH=D:\qt\5.12.4\msvc2017_64\bin;%PATH%

del /q robot_comm_test.log robot_comm_test_full.log 2>nul

if /I "%MODE%"=="usb" (
  robot_comm_test.exe -H 192.168.1.20 -p 43100 -i "%INI%" -l DB2666828 -t %HOLD% -v --camera-usb 0,1 > stdout_%MODE%.txt 2>&1
) else if /I "%MODE%"=="imu" (
  robot_comm_test.exe -H 192.168.1.20 -p 43100 -i "%INI%" -l DB2666828 -t %HOLD% -v --imu > stdout_%MODE%.txt 2>&1
) else (
  robot_comm_test.exe -H 192.168.1.20 -p 43100 -i "%INI%" -l DB2666828 -t %HOLD% -v > stdout_%MODE%.txt 2>&1
)

echo RC=%errorlevel%
type stdout_%MODE%.txt
