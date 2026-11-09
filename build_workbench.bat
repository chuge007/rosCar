@echo off
setlocal

rem Release build of PA1664Workbench (Qt 6.8.3 + MSVC 2022).
rem Shadow build lives inside the workspace; output goes to bin.

set "ROOT=%~dp0"
set "QTDIR=D:\qt5.15.2\6.8.3\msvc2022_64"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "BUILDDIR=%ROOT%build\workbench_release"

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [FAIL] vcvars64
  exit /b 1
)

set "PATH=%QTDIR%\bin;%PATH%"

if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
cd /d "%BUILDDIR%"

qmake "%ROOT%PA1664Workbench.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 (
  echo [FAIL] qmake
  exit /b 1
)

if exist Makefile.Release (
  set "MAKEFILE_ARG=-f Makefile.Release"
) else (
  set "MAKEFILE_ARG="
)

if /I "%~1"=="clean" (
  echo [INFO] clean rebuild
  nmake /nologo %MAKEFILE_ARG% clean
  if errorlevel 1 (
    echo [FAIL] nmake clean
    exit /b 1
  )
)

nmake /nologo %MAKEFILE_ARG%
if errorlevel 1 (
  echo [FAIL] nmake
  exit /b 1
)

echo [OK] PA1664Workbench built
exit /b 0
