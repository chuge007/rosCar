@echo off
setlocal
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0StartOperatorWorkbench.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo.
    echo PA1664Workbench failed to start. See runtime\launcher.log.
    pause
)
exit /b %RC%