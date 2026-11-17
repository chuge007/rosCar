@echo off
setlocal

rem 编译两个测试工程：静态库版 + 动态库版。
rem 依赖 examples\sdk（由 _prepare_sdk.py 从 api\小车api 复制）。

if "%PA1664_QTDIR%"=="" set "PA1664_QTDIR=D:\qt5.15.2\6.8.3\msvc2022_64"
if "%PA1664_VCVARS%"=="" set "PA1664_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

set "QTDIR=%PA1664_QTDIR%"
set "VCVARS=%PA1664_VCVARS%"
set "ROOT=%~dp0"

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [FAIL] vcvars64
  exit /b 1
)
set "PATH=%QTDIR%\bin;%PATH%"

if not exist "%ROOT%sdk\static\PA1664RobotApi.lib" (
  echo [FAIL] sdk not prepared, run: python _prepare_sdk.py
  exit /b 1
)

if not exist "%ROOT%build_static_release" mkdir "%ROOT%build_static_release"
cd /d "%ROOT%build_static_release"
qmake "%ROOT%test_static.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 (
  echo [FAIL] qmake static
  exit /b 1
)
nmake /nologo
if errorlevel 1 (
  echo [FAIL] nmake static
  exit /b 1
)
echo [OK] bin_static\api_test_static.exe

if not exist "%ROOT%build_shared_release" mkdir "%ROOT%build_shared_release"
cd /d "%ROOT%build_shared_release"
qmake "%ROOT%test_shared.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 (
  echo [FAIL] qmake shared
  exit /b 1
)
nmake /nologo
if errorlevel 1 (
  echo [FAIL] nmake shared
  exit /b 1
)
copy /Y "%ROOT%sdk\shared\PA1664RobotApi.dll" "%ROOT%bin_shared\" >nul
if errorlevel 1 (
  echo [FAIL] copy dll
  exit /b 1
)
echo [OK] bin_shared\api_test_shared.exe (+ PA1664RobotApi.dll)
exit /b 0
