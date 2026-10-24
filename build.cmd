@echo off
setlocal
set "VS_VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" set "VS_VCVARS=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" (
    echo Visual Studio 2022 C++ x64 build environment was not found.
    exit /b 1
)
call "%VS_VCVARS%"
if errorlevel 1 exit /b 1
set "TEMP=D:\QtTemp"
set "TMP=D:\QtTemp"
if not exist "D:\QtTemp" mkdir "D:\QtTemp"
cd /d "%~dp0"
set "EXTRA_CONFIG="
if /I "%~1"=="staging" set "EXTRA_CONFIG=CONFIG+=staging"
set "QT_BIN=D:\qt5.15.2\6.8.3\msvc2022_64\bin"
if not exist "%QT_BIN%\qmake.exe" set "QT_BIN=D:\QT\6.8.3\msvc2022_64\bin"
if not exist "%QT_BIN%\qmake.exe" (
    echo Qt 6.8.3 MSVC 2022 x64 qmake was not found.
    exit /b 1
)
set "JOM=D:\qt\Tools\QtCreator\bin\jom.exe"
if not exist "%JOM%" set "JOM=D:\QT\Tools\QtCreator\bin\jom\jom.exe"
if not exist "%JOM%" (
    echo jom was not found.
    exit /b 1
)
"%QT_BIN%\qmake.exe" PA1664Workbench.pro -spec win32-msvc CONFIG+=release %EXTRA_CONFIG%
if errorlevel 1 exit /b 1
"%JOM%" -j4
if errorlevel 1 exit /b 1
if /I "%~2"=="nodeploy" exit /b 0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy-runtime.ps1"
exit /b %errorlevel%
