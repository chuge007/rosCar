@echo off
setlocal EnableExtensions
set "SCRIPT_DIR=%~dp0"
rem SSH settings for the target board. Set SSH_PASSWORD in the environment
rem before running this helper; credentials are intentionally not stored here.
set "SSH_HOST=192.168.1.20"
set "SSH_USER=pc"
set "SSH_PORT=22"
set "SSH_TARGET_ROOT=C:/Users/pc/Desktop/target_board_release/target_board_release"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%ssh_incremental_update.ps1" ^
  -SshHost "%SSH_HOST%" ^
  -SshUser "%SSH_USER%" ^
  -SshPort %SSH_PORT% ^
  -SshPassword "%SSH_PASSWORD%" ^
  -SshTargetRoot "%SSH_TARGET_ROOT%" %*
exit /b %ERRORLEVEL%
