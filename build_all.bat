@echo off
REM Build script for PA1664Workbench with Camera Server

echo Building PA1664CameraServer...
qmake PA1664CameraServer.pro
if errorlevel 1 (
    echo Failed to generate PA1664CameraServer Makefile
    exit /b 1
)
nmake clean
nmake release
if errorlevel 1 (
    echo Failed to build PA1664CameraServer
    exit /b 1
)

echo.
echo Building PA1664Workbench...
qmake PA1664Workbench.pro
if errorlevel 1 (
    echo Failed to generate PA1664Workbench Makefile
    exit /b 1
)
nmake clean
nmake release
if errorlevel 1 (
    echo Failed to build PA1664Workbench
    exit /b 1
)

echo.
echo Build complete!
echo Executables:
echo   - bin\PA1664CameraServer.exe
echo   - bin\PA1664Workbench.exe
