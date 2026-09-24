@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "TEMP=D:\QtTemp"
set "TMP=D:\QtTemp"
if not exist "D:\QtTemp" mkdir "D:\QtTemp"
cd /d "%~dp0"
set "EXTRA_CONFIG="
if /I "%~1"=="staging" set "EXTRA_CONFIG=CONFIG+=staging"
"D:\QT\6.8.3\msvc2022_64\bin\qmake.exe" PA1664Workbench.pro -spec win32-msvc CONFIG+=release %EXTRA_CONFIG%
if errorlevel 1 exit /b 1
"D:\QT\Tools\QtCreator\bin\jom\jom.exe" -j4
if errorlevel 1 exit /b 1
if /I "%~2"=="nodeploy" exit /b 0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy-runtime.ps1"
exit /b %errorlevel%
