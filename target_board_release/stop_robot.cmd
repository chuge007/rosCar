@echo off
setlocal EnableExtensions
set "RELEASE_DIR=%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RELEASE_DIR%stop_robot.ps1"
exit /b %ERRORLEVEL%
