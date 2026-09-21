@echo off
setlocal EnableExtensions
set "SCRIPT_DIR=%~dp0"
rem Select the target explicitly when calling this helper. The PowerShell
rem script chooses the matching private key automatically:
rem   192.168.1.20 -> %USERPROFILE%\.ssh\id_ed25519
rem   192.168.1.21 -> %USERPROFILE%\.ssh\id_ed25519_robot21
set "SSH_HOST=192.168.1.20"
set "SSH_USER=pc"
set "SSH_PORT=22"
set "SSH_TARGET_ROOT=C:/Users/pc/Desktop/target_board_release/target_board_release"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%ssh_incremental_update.ps1" ^
  -SshHost "%SSH_HOST%" ^
  -SshUser "%SSH_USER%" ^
  -SshPort %SSH_PORT% ^
  -SshTargetRoot "%SSH_TARGET_ROOT%" %*
exit /b %ERRORLEVEL%
