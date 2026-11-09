@echo off
chcp 65001 >nul

rem Apply drive_settings.ini to the host, then connect laser + both cameras
rem and keep the control session open for 45s.

cd /d "%~dp0release"
set PATH=D:\qt\5.12.4\msvc2017_64\bin;%PATH%

set "INI=%~1"
if "%INI%"=="" set "INI=drive_settings.ini"

del /q robot_comm_test.log 2>nul
robot_comm_test.exe -H 192.168.1.20 -p 43100 -i "%INI%" -l DB2666828 -t 45 > stdout_connect.txt 2>&1
echo RC=%errorlevel%
type stdout_connect.txt
