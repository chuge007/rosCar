@echo off
setlocal EnableExtensions
set "RELEASE_DIR=%~dp0"
pushd "%RELEASE_DIR%" >NUL
if errorlevel 1 exit /b 1
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RELEASE_DIR%run_drive_test.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"
popd
if not "%EXIT_CODE%"=="0" pause
exit /b %EXIT_CODE%
