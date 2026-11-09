@echo off
setlocal

rem Syntax-only verification of the modified translation units (Qt 6.8.3 + MSVC 2022).
set "ROOT=%~dp0"
set "QTDIR=D:\qt5.15.2\6.8.3\msvc2022_64"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

cd /d "%ROOT%"
set "PATH=%QTDIR%\bin;%PATH%"

nmake /nologo -f Makefile.Release build\obj\robot_usb_camera_controller.obj
if errorlevel 1 (
  echo [FAIL] robot_usb_camera_controller.obj
  exit /b 1
)
echo [OK] robot_usb_camera_controller.obj
exit /b 0
