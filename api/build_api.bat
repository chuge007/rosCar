@echo off
setlocal

rem 编译 api 静态库（默认同时编译示例）。用法：
rem   build_api.bat          编译 lib + example
rem   build_api.bat libonly  只编译 lib

set "ROOT=%~dp0"
set "QTDIR=D:\qt5.15.2\6.8.3\msvc2022_64"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "LIBDIR=%ROOT%build\lib_release"
set "DEMODIR=%ROOT%examples\build\demo"

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [FAIL] vcvars64
  exit /b 1
)

set "PATH=%QTDIR%\bin;%PATH%"

if not exist "%LIBDIR%" mkdir "%LIBDIR%"
cd /d "%LIBDIR%"
qmake "%ROOT%PA1664RobotApi.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 (
  echo [FAIL] qmake lib
  exit /b 1
)
nmake /nologo
if errorlevel 1 (
  echo [FAIL] nmake lib
  exit /b 1
)
echo [OK] api\lib\PA1664RobotApi.lib

if /I "%~1"=="shared" (
  if not exist "%ROOT%build\shared_release" mkdir "%ROOT%build\shared_release"
  cd /d "%ROOT%build\shared_release"
  qmake "%ROOT%PA1664RobotApi.pro" -spec win32-msvc "CONFIG+=release" "CONFIG+=pa1664_shared"
  if errorlevel 1 (
    echo [FAIL] qmake shared
    exit /b 1
  )
  nmake /nologo
  if errorlevel 1 (
    echo [FAIL] nmake shared
    exit /b 1
  )
  echo [OK] build\shared\PA1664RobotApi.dll
  exit /b 0
)

if /I "%~1"=="libonly" exit /b 0

if not exist "%DEMODIR%" mkdir "%DEMODIR%"
cd /d "%DEMODIR%"
qmake "%ROOT%examples\examples.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 (
  echo [FAIL] qmake example
  exit /b 1
)
nmake /nologo
if errorlevel 1 (
  echo [FAIL] nmake example
  exit /b 1
)
echo [OK] api\examples\bin\forward_demo.exe
exit /b 0
