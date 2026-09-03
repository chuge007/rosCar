[CmdletBinding()]
param(
    [switch]$UseSlcanCanBridge,
    [switch]$SkipCanopenAxis,
    [switch]$SkipLaserPathFollower,
    [switch]$SkipMonitor,
    [string]$ParametersFile = ""
)

$ErrorActionPreference = "Stop"
$releaseRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$runtimeRoot = Join-Path $releaseRoot "runtime"
$installRoot = Join-Path $releaseRoot "install"
$vendorRoot = Join-Path $releaseRoot "vendor\mv3dlp_sdk"

# Incremental uploads use .new files when Windows has the running executable
# locked. Once the previous stack has stopped, activate them before launch.
$pendingExecutables = @(
    "crawling_robot_drivers\rim302_imu_node.exe",
    "crawling_robot_laser\mv3dlp_laser_node.exe"
)
foreach ($relativePath in $pendingExecutables) {
    $destination = Join-Path $installRoot "lib\$relativePath"
    $pending = "$destination.new"
    if (Test-Path -LiteralPath $pending) {
        Move-Item -LiteralPath $pending -Destination $destination -Force
        Write-Host "Activated pending update: install\lib\$relativePath" -ForegroundColor Green
    }
}

function Fail([string]$message) {
    Write-Host "[ERROR] $message" -ForegroundColor Red
    Write-Host "See README.md and run packaging\build_release.ps1 on the development PC if this is a new package."
    exit 2
}

# Resolve a parameter path relative to this release folder, even when the
# launcher is started from a different working directory.
function Resolve-ReleasePath([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return $null }
    if ([IO.Path]::IsPathRooted($path)) { return (Resolve-Path -LiteralPath $path).Path }
    return (Resolve-Path -LiteralPath (Join-Path $releaseRoot $path)).Path
}

if (-not (Test-Path (Join-Path $runtimeRoot "local_setup.bat")) -and
    -not (Test-Path (Join-Path $runtimeRoot "setup.bat"))) {
    Fail "Bundled ROS 2 runtime is missing from '$runtimeRoot'."
}
if (-not (Test-Path (Join-Path $installRoot "local_setup.bat")) -and
    -not (Test-Path (Join-Path $installRoot "setup.bat"))) {
    Fail "Built ROS overlay is missing from '$installRoot'."
}

$requiredExecutables = @(
    "crawling_robot_drivers\base_drive_node.exe",
    "crawling_robot_drivers\rim302_imu_node.exe",
    "crawling_robot_drivers\modbus_encoder_node.exe",
    "crawling_robot_drivers\encoder_odom_node.exe",
    "crawling_robot_drivers\canopen_axis_node.exe",
    "crawling_robot_laser\mv3dlp_laser_node.exe",
    "crawling_robot_control\laser_path_follower_node.exe"
)
foreach ($relativePath in $requiredExecutables) {
    if (-not (Test-Path (Join-Path $installRoot "lib\$relativePath"))) {
        Fail "Required executable is missing: install\lib\$relativePath"
    }
}

if (-not (Test-Path (Join-Path $vendorRoot "Mv3dLp.dll"))) {
    Fail "MV3DLP SDK runtime is missing from '$vendorRoot'."
}

$setupScript = if (Test-Path (Join-Path $runtimeRoot "local_setup.bat")) {
    Join-Path $runtimeRoot "local_setup.bat"
} else {
    Join-Path $runtimeRoot "setup.bat"
}
$overlaySetupScript = if (Test-Path (Join-Path $installRoot "local_setup.bat")) {
    Join-Path $installRoot "local_setup.bat"
} else {
    Join-Path $installRoot "setup.bat"
}
$configPath = if ([string]::IsNullOrWhiteSpace($ParametersFile)) {
    Join-Path $releaseRoot "config\robot.yaml"
} else {
    Resolve-ReleasePath $ParametersFile
}
if (-not (Test-Path $configPath)) {
    Fail "Parameter file does not exist: $configPath"
}

function Get-SectionLines([string[]]$lines, [string]$sectionName) {
    $sectionLines = New-Object System.Collections.Generic.List[string]
    $startIndex = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].TrimEnd() -eq "${sectionName}:") {
            $startIndex = $index + 1
            break
        }
    }
    if ($startIndex -lt 0) {
        return @()
    }
    for ($index = $startIndex; $index -lt $lines.Count; ++$index) {
        $line = $lines[$index]
        if ($line -match '^[^\s]') {
            break
        }
        $sectionLines.Add($line.Trim())
    }
    return $sectionLines
}

$configLines = Get-Content -LiteralPath $configPath
$canopenSection = Get-SectionLines $configLines "canopen_axis_node"
if ($canopenSection.Count -gt 0) {
    $canopenPlaceholders = @(
        'lower_limits_m: [0.0, 0.0, 0.0]',
        'upper_limits_m: [0.0, 0.0, 0.0]',
        'max_velocities_m_s: [0.0, 0.0, 0.0]',
        'max_accelerations_m_s2: [0.0, 0.0, 0.0]',
        'counts_per_meter: [0.0, 0.0, 0.0]'
    )
    $canopenNeedsConfig = $true
    foreach ($line in $canopenPlaceholders) {
        if ($canopenSection -notcontains $line) {
            $canopenNeedsConfig = $false
            break
        }
    }
    if ($canopenNeedsConfig) {
        $SkipCanopenAxis = $true
        Write-Host "[WARN] Auto-skipping canopen_axis_node because the release config still contains placeholder motion limits."
    }
}

$laserSection = Get-SectionLines $configLines "mv3dlp_laser_node"
if ($laserSection.Count -gt 0 -and
    ($laserSection -contains 'serial_number: ""') -and
    ($laserSection -contains 'device_ip: ""')) {
    $SkipLaserPathFollower = $true
    Write-Host "[WARN] Auto-skipping laser_path_follower_node because the laser config is still empty."
}

$pidFile = Join-Path $releaseRoot "run.pid"
if (Test-Path $pidFile) {
    $oldPid = 0
    try { $oldPid = [int](Get-Content -Raw -LiteralPath $pidFile) } catch {}
    if ($oldPid -gt 0 -and (Get-Process -Id $oldPid -ErrorAction SilentlyContinue)) {
        Fail "ROS 2 stack is already running (PID $oldPid). Use stop_robot.cmd first."
    }
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
}

$env:ROS_DOMAIN_ID = if ($env:ROS_DOMAIN_ID) { $env:ROS_DOMAIN_ID } else { "0" }
$env:RMW_IMPLEMENTATION = if ($env:RMW_IMPLEMENTATION) { $env:RMW_IMPLEMENTATION } else { "rmw_fastrtps_cpp" }
$env:ROS_LOCALHOST_ONLY = if ($env:ROS_LOCALHOST_ONLY) { $env:ROS_LOCALHOST_ONLY } else { "0" }
$env:ROS_AUTOMATIC_DISCOVERY_RANGE = if ($env:ROS_AUTOMATIC_DISCOVERY_RANGE) { $env:ROS_AUTOMATIC_DISCOVERY_RANGE } else { "SUBNET" }
$fastDdsProfile = Join-Path $releaseRoot "config\fastdds_profile.xml"
if (Test-Path -LiteralPath $fastDdsProfile) {
    # The target has two 192.168.1.x adapters. Advertise only the cable-side
    # address so DDS replies do not go through the other adapter.
    $env:FASTRTPS_DEFAULT_PROFILES_FILE = $fastDdsProfile
}
$env:MV3DLP_LIBRARY_PATH = Join-Path $vendorRoot "Mv3dLp.dll"
$bundledPython = @(
    (Join-Path $runtimeRoot "python.exe"),
    (Join-Path $runtimeRoot "Scripts\python.exe"),
    (Join-Path $runtimeRoot "python\python.exe"),
    (Join-Path $runtimeRoot "python\Scripts\python.exe")
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $bundledPython) {
    Fail "Bundled ROS 2 runtime has no python.exe; this release cannot run without installing Python on the target board."
}
$ros2Script = Join-Path $runtimeRoot "Scripts\ros2-script.py"
if (-not (Test-Path -LiteralPath $ros2Script)) {
    Fail "Bundled ROS 2 CLI script is missing: $ros2Script"
}
$env:COLCON_PYTHON_EXECUTABLE = $bundledPython
$pythonRoot = Split-Path -Parent $bundledPython
$env:PATH = "$vendorRoot;$runtimeRoot;$pythonRoot;$pythonRoot\Scripts;$pythonRoot\Library\bin;$runtimeRoot\bin;$runtimeRoot\Library\bin;$env:PATH"

$launchArguments = "crawling_robot_bringup robot.launch.py parameters_file:=`"$configPath`""
if ($UseSlcanCanBridge) {
    $launchArguments += " use_slcan_can_bridge:=true"
}
if ($SkipCanopenAxis) {
    $launchArguments += " use_canopen_axis:=false"
}
if ($SkipLaserPathFollower) {
    $launchArguments += " use_laser_path_follower:=false"
}
# Windows launch_ros tries to execute Python console entry points as native
# executables. Start the monitor explicitly after sourcing both ROS prefixes.
$launchArguments += " use_monitor:=false"

# The generated ros2.exe launcher embeds the Python path from the development
# machine. Invoke its script with the bundled interpreter so the whole folder
# remains relocatable after it is copied to a target board.
$ros2Command = "`"$bundledPython`" `"$ros2Script`""
$monitorLauncher = Join-Path $releaseRoot "start_monitor.cmd"
if (-not $SkipMonitor) {
    if (-not (Test-Path -LiteralPath $monitorLauncher)) {
        Fail "Monitor launcher is missing: $monitorLauncher"
    }
    $monitorStart = "start `"Crawling Robot Monitor`" /D `"$releaseRoot`" `"$monitorLauncher`""
    $command = "call `"$setupScript`" && call `"$overlaySetupScript`" && $monitorStart && $ros2Command launch $launchArguments"
} else {
    $command = "call `"$setupScript`" && call `"$overlaySetupScript`" && $ros2Command launch $launchArguments"
}
Write-Host "Starting crawling robot ROS 2 stack..." -ForegroundColor Cyan
Write-Host "ROS_DOMAIN_ID=$env:ROS_DOMAIN_ID"
Write-Host "ROS_LOCALHOST_ONLY=$env:ROS_LOCALHOST_ONLY"
Write-Host "RMW_IMPLEMENTATION=$env:RMW_IMPLEMENTATION"
Write-Host "Parameters=$configPath"
if (-not $SkipMonitor) {
    Write-Host "Monitor=$monitorLauncher"
}
Write-Host "Press Ctrl+C in this window to stop."

# Keep the child command attached to this console so Ctrl+C behaves normally.
# stop_robot.cmd can still terminate the complete process tree using this PID.
$myPid = $PID
Set-Content -LiteralPath $pidFile -Value $myPid -Encoding ASCII
try {
    & $env:ComSpec /d /s /c $command
    $exitCode = $LASTEXITCODE
} finally {
    $recordedPid = ""
    try { $recordedPid = (Get-Content -Raw -LiteralPath $pidFile).Trim() } catch {}
    if ($recordedPid -eq "$myPid") {
        Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    }
}
exit $exitCode
