@echo off
setlocal EnableExtensions
set "SCRIPT_DIR=%~dp0"

rem Build every ROS package incrementally and refresh the local release overlay.
rem Extra arguments are forwarded, for example -TargetRoot "\\192.168.1.50\robot_release\target_board_release".
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%incremental_update.ps1" %*
exit /b %ERRORLEVEL%
