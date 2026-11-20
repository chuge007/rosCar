@echo off
chcp 936 >nul
setlocal
cd /d "%~dp0"

echo ============================================
echo   PA1664 小车端主机
echo ============================================
echo.
echo 开机自启动由主机程序自己维护；这里只负责现在启动。
echo 日志：app\logs\robot_host.log（可运行 查看日志.ps1）
echo.

if not exist "app\PA1664RobotHost.exe" (
    echo [错误] 找不到 app\PA1664RobotHost.exe，请确认目录完整。
    pause
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File ".\StartRobotHost.ps1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo [失败] 启动返回 %RC%，详见 runtime\launcher.log
pause
exit /b %RC%
