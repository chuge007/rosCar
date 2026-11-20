[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$appExe = Join-Path $root 'app\PA1664RobotHost.exe'
if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) {
    $appExe = Join-Path $root 'PA1664RobotHost.exe'
}
if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) {
    throw 'PA1664RobotHost.exe was not found next to this script.'
}

# The startup shortcut is owned by the host itself now: every launch verifies it
# and rewrites it when it is missing or points somewhere else. This script only
# forces a refresh and brings the host up right away. If a host is already
# running, the new process exits on the single-instance lock and nothing breaks.
Start-Process -FilePath $appExe -ArgumentList '--autostart' `
    -WorkingDirectory (Split-Path -Parent $appExe) -WindowStyle Hidden

Write-Host 'PA1664 Robot Host started with --autostart.'
Write-Host 'The logon shortcut is maintained by the host itself; no manual reinstall is needed after replacing the exe.'
exit 0
