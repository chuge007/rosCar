@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_portable.ps1" %*
exit /b %ERRORLEVEL%
