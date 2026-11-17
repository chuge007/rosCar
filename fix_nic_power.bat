@echo off
chcp 65001 >nul
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
    echo 需要管理员权限，正在申请...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo ============================================
echo  采集网卡状态并关闭节能（单次执行，可回滚）
echo ============================================
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0robot_net_diag.ps1" -Fix

echo.
echo 完成。报告：%~dp0net_diag.txt
echo 回滚：报告里 BEFORE 段记录了每一项的原值。
echo.
pause
