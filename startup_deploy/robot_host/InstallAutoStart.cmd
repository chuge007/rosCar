@echo off
setlocal
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0InstallAutoStart.ps1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo Autostart installation failed.
if "%RC%"=="0" echo Autostart installation completed.
pause
exit /b %RC%