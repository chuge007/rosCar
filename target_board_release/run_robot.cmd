@echo off
setlocal EnableExtensions

set "RELEASE_DIR=%~dp0"
pushd "%RELEASE_DIR%" >NUL
if errorlevel 1 (
  echo [ERROR] Cannot enter release directory: "%RELEASE_DIR%"
  exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RELEASE_DIR%run_robot.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"
popd
if not "%EXIT_CODE%"=="0" (
  echo.
  echo [ERROR] Crawling Robot failed to start or stopped with exit code %EXIT_CODE%.
  pause
)
exit /b %EXIT_CODE%
