@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0bin"
if not exist "PA1664Workbench.exe" (
  echo [错误] 找不到 bin\PA1664Workbench.exe，请先执行 build_workbench.bat 编译。
  exit /b 1
)
echo 连接小车端主机 192.168.1.20:43100 ...
start "" PA1664Workbench.exe --remote 192.168.1.20:43100
endlocal
