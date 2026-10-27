# 日志快速查看工具
param([switch]$Live)

if ($Live) {
    Write-Host "=== 实时监控日志（按 Ctrl+C 停止）===" -ForegroundColor Green
    if (Test-Path "RobotControl\logs\robot_console.log") {
        Get-Content "RobotControl\logs\robot_console.log" -Wait -Tail 20
    } else {
        Write-Host "日志文件不存在" -ForegroundColor Red
    }
} else {
    Write-Host "=== 最新日志 (最后50行) ===" -ForegroundColor Green
    if (Test-Path "RobotControl\logs\robot_console.log") {
        Get-Content "RobotControl\logs\robot_console.log" -Tail 50 -Encoding UTF8
    } elseif (Test-Path "logs\workbench_console.log") {
        Get-Content "logs\workbench_console.log" -Tail 50 -Encoding UTF8
    } else {
        Write-Host "日志文件不存在" -ForegroundColor Red
    }
}
