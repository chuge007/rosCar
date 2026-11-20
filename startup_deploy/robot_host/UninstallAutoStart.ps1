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

# Deleting the shortcut by hand is not enough any more: the host rewrites it on
# every launch. --remove-autostart deletes the shortcut AND drops a
# .autostart-off marker next to the exe, so subsequent launches stay quiet.
# Use --autostart (or InstallAutoStart) to turn it back on.
& $appExe --remove-autostart
$code = $LASTEXITCODE

# The desktop shortcut is outside the host's own responsibility.
$shortcutName = 'PA1664 Robot Host.lnk'
$desktop = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
$desktopPath = Join-Path $desktop $shortcutName
if (Test-Path -LiteralPath $desktopPath -PathType Leaf) {
    Remove-Item -LiteralPath $desktopPath -Force
    Write-Host ('Removed desktop shortcut: ' + $desktopPath)
}

Write-Host 'Autostart disabled. The running Host process was not stopped.'
exit $code
