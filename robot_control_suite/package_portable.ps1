[CmdletBinding()]
param(
    [string]$Destination = "",
    [string]$QtRoot = "D:\qt\5.12.4\msvc2017_64",
    [string]$RosRoot = "D:\dev\CrawlingRobot\target_board_release\runtime",
    [string]$OverlayRoot = "D:\dev\CrawlingRobot\target_board_release\install"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $PSScriptRoot "robot_control_suite\release\robot_control_suite.exe"
$outputRoot = if ([string]::IsNullOrWhiteSpace($Destination)) {
    Join-Path $repoRoot "robot_control_suite_portable"
} else {
    [IO.Path]::GetFullPath($Destination)
}

function Require-Path([string]$path, [string]$description) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing $description`: $path" }
}

Require-Path $exe "desktop executable"
Require-Path (Join-Path $QtRoot "bin\windeployqt.exe") "windeployqt"
Require-Path (Join-Path $RosRoot "local_setup.bat") "ROS runtime"
Require-Path (Join-Path $RosRoot "Scripts\ros2-script.py") "ROS 2 CLI script"
Require-Path (Join-Path $OverlayRoot "local_setup.bat") "ROS overlay"

if (Test-Path -LiteralPath $outputRoot) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination (Join-Path $outputRoot "robot_control_suite.exe") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "desktop\cmd_vel_bridge.py") -Destination (Join-Path $outputRoot "cmd_vel_bridge.py") -Force
$windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"
& $windeployqt --release --no-translations --no-compiler-runtime (Join-Path $outputRoot "robot_control_suite.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

& robocopy.exe $RosRoot (Join-Path $outputRoot "ros2-window") /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -gt 7) { throw "ROS runtime copy failed (exit $LASTEXITCODE)" }
& robocopy.exe $OverlayRoot (Join-Path $outputRoot "ros2-overlay") /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -gt 7) { throw "ROS overlay copy failed (exit $LASTEXITCODE)" }

New-Item -ItemType Directory -Path (Join-Path $outputRoot "config") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "portable_fastdds_profile.xml") -Destination (Join-Path $outputRoot "config\fastdds_profile.xml") -Force
Set-Content -LiteralPath (Join-Path $outputRoot "start_robot_control.cmd") -Encoding ASCII -Value @'
@echo off
setlocal
set "APP_DIR=%~dp0"
set "ROS2_WINDOW_ROOT=%APP_DIR%ros2-window"
set "ROBOT_CONTROL_WS_INSTALL=%APP_DIR%ros2-overlay"
set "ROS_LOCALHOST_ONLY=1"
set "ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST"
start "Robot Control Suite" /D "%APP_DIR%" "%APP_DIR%robot_control_suite.exe"
'@
Set-Content -LiteralPath (Join-Path $outputRoot "README.txt") -Encoding UTF8 -Value @'
Robot Control Suite portable package

This package is configured for local ROS 2 testing on the same Windows computer.
Start the local motor stack first with target_board_release\run_drive_test.cmd;
it auto-detects the USB-CAN adapter and keeps DDS on localhost. Then run
start_robot_control.cmd from this folder.

The package includes Qt, ROS 2, Python, the ROS interface overlay, and DDS settings.
No remote host or LAN discovery is used by the packaged control software.
'@

Write-Host "Portable desktop package created: $outputRoot" -ForegroundColor Green
