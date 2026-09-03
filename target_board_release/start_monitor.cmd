@echo off
setlocal EnableExtensions
set "RELEASE_DIR=%~dp0"
call "%RELEASE_DIR%runtime\local_setup.bat"
call "%RELEASE_DIR%install\local_setup.bat"
"%RELEASE_DIR%runtime\python\python.exe" "%RELEASE_DIR%install\lib\crawling_robot_monitor\robot_monitor-script.py"
exit /b %ERRORLEVEL%
