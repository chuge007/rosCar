param(
    [string]$Target = '192.168.1.20',
    [int]$IntervalSeconds = 2,
    [int]$Minutes = 480
)
$ErrorActionPreference = 'SilentlyContinue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir = Join-Path $root 'bin\logs'
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
$log = Join-Path $logDir 'link_watch.log'
$pidFile = Join-Path $logDir 'link_watch.pid'

Set-Content -Path $pidFile -Value $PID
$deadline = (Get-Date).AddMinutes($Minutes)
$last = $null
Add-Content -Path $log -Value ("{0}  start watching {1} every {2}s for {3}min (pid {4})" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Target, $IntervalSeconds, $Minutes, $PID)

while ((Get-Date) -lt $deadline) {
    $ok = Test-Connection -ComputerName $Target -Count 1 -Quiet
    if ($null -eq $last -or $ok -ne $last) {
        $state = if ($ok) { 'UP' } else { 'DOWN' }
        Add-Content -Path $log -Value ("{0}  {1}  {2}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Target, $state)
        $last = $ok
    }
    Start-Sleep -Seconds $IntervalSeconds
}
Add-Content -Path $log -Value ("{0}  stop watching" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Remove-Item -Path $pidFile -ErrorAction SilentlyContinue
