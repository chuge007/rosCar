param([switch]$Live)

$root = $PSScriptRoot
$hostLog = Join-Path $root 'app\logs\robot_host.log'
$launcherLog = Join-Path $root 'runtime\launcher.log'

if ($Live) {
    if (-not (Test-Path -LiteralPath $hostLog)) {
        Write-Host ('日志还没生成：' + $hostLog) -ForegroundColor Red
        Read-Host '按回车关闭'
        exit 1
    }
    Write-Host '=== 实时跟踪 robot_host.log（Ctrl+C 停止）===' -ForegroundColor Green
    Get-Content -LiteralPath $hostLog -Wait -Tail 30 -Encoding UTF8
    exit 0
}

Write-Host ('=== 主机日志 ' + $hostLog + '（最后 50 行）===') -ForegroundColor Green
if (Test-Path -LiteralPath $hostLog) {
    Get-Content -LiteralPath $hostLog -Tail 50 -Encoding UTF8
}
else {
    Write-Host '日志文件尚未生成（主机还没运行过）' -ForegroundColor Yellow
}

Write-Host ''
Write-Host ('=== 启动器日志 ' + $launcherLog + '（最后 20 行）===') -ForegroundColor Green
if (Test-Path -LiteralPath $launcherLog) {
    Get-Content -LiteralPath $launcherLog -Tail 20 -Encoding UTF8
}
else {
    Write-Host '启动器日志尚未生成' -ForegroundColor Yellow
}

Write-Host ''
Write-Host '提示：加 -Live 参数可实时跟踪。' -ForegroundColor Cyan
Read-Host '按回车关闭'
exit 0
