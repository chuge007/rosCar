@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0bin"
if not exist "PA1664Workbench.exe" (
  echo [错误] 找不到 bin\PA1664Workbench.exe，请先执行 build_workbench.bat 编译。
  exit /b 1
)
netstat -ano | findstr ":43100" | findstr "LISTENING" >nul
if errorlevel 1 (
  echo [提示] 本机 43100 没有监听。若要走本机联调，请先启动 bin\PA1664RobotHost.exe。
  echo        本机没有超声板卡时，本机 Host 只能跑空壳，采集会一直是 0 帧。
  echo.
  pause
)
echo 连接本机主机 127.0.0.1:43100 ...
start "" PA1664Workbench.exe --remote 127.0.0.1:43100
endlocal
