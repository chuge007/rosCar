@echo off
setlocal

set "SRC=%~dp0"
set "QTDIR=D:\qt\5.12.4\msvc2017_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [ERROR] vcvars64.bat failed
  exit /b 1
)

cd /d "%SRC%"
"%QTDIR%\bin\qmake.exe" robot_comm_test.pro
if errorlevel 1 exit /b 1

nmake /nologo
if errorlevel 1 exit /b 1

echo [OK] built: %SRC%release\robot_comm_test.exe
exit /b 0
