[CmdletBinding()]
param(
    [string]$ParametersFile = "config\drive_test.yaml"
)

$ErrorActionPreference = "Stop"
$releaseRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$runtimeRoot = Join-Path $releaseRoot "runtime"
$installRoot = Join-Path $releaseRoot "install"
$configPath = if ([IO.Path]::IsPathRooted($ParametersFile)) {
    (Resolve-Path -LiteralPath $ParametersFile).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $releaseRoot $ParametersFile)).Path
}

$runtimeSetup = Join-Path $runtimeRoot "local_setup.bat"
$overlaySetup = Join-Path $installRoot "local_setup.bat"
$python = Join-Path $runtimeRoot "python\python.exe"
$ros2Script = Join-Path $runtimeRoot "Scripts\ros2-script.py"
$launchFile = Join-Path $installRoot "share\crawling_robot_bringup\launch\drive_test.launch.py"
$monitorLauncher = Join-Path $releaseRoot "start_monitor.cmd"
$pidFile = Join-Path $releaseRoot "run.pid"

$required = @(
    $runtimeSetup,
    $overlaySetup,
    $python,
    $ros2Script,
    $launchFile,
    $monitorLauncher,
    $configPath,
    (Join-Path $installRoot "lib\crawling_robot_drivers\base_drive_node.exe"),
    (Join-Path $installRoot "lib\crawling_robot_drivers\slcan_can_bridge_node.exe")
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required drive-test file is missing: $path"
    }
}

$canPortInfo = @(Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue) |
    Where-Object { "$($_.Name) $($_.Description) $($_.PNPDeviceID)" -match '(?i)CANable|SLCAN|VID_16D0&PID_117E' } |
    Select-Object -First 1
$canPort = if ($canPortInfo) { [string]$canPortInfo.DeviceID } else { "" }
if (-not $canPort) {
    # Fall back to generic Ports/PNP records used by some USB-CAN drivers.
    $records = @()
    if (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) {
        $records += @(Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue)
    }
    $records += @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { "$($_.Name) $($_.Caption) $($_.PNPDeviceID)" -match '(?i)CANable|SLCAN|VID_16D0&PID_117E' })
    foreach ($record in $records) {
        $text = "$($record.FriendlyName) $($record.Name) $($record.Caption) $($record.InstanceId) $($record.PNPDeviceID)"
        if ($text -notmatch '(?i)CANable|SLCAN|VID_16D0&PID_117E') { continue }
        $com = [regex]::Match($text, '(?i)\bCOM\d+\b')
        if ($com.Success) {
            $canPort = $com.Value.ToUpperInvariant()
            break
        }
    }
}
if ($canPort) {
    Write-Host "Detected USB-CAN adapter on $canPort"
    $env:CRAWLING_ROBOT_CAN_PORT = $canPort
} else {
    Write-Host "USB-CAN adapter not detected; launching ROS services in offline mode." -ForegroundColor Yellow
    Remove-Item Env:CRAWLING_ROBOT_CAN_PORT -ErrorAction SilentlyContinue
}

if (Test-Path -LiteralPath $pidFile) {
    $oldPid = 0
    try { $oldPid = [int](Get-Content -Raw -LiteralPath $pidFile) } catch {}
    $oldProcess = if ($oldPid -gt 0 -and $oldPid -ne $PID) {
        Get-CimInstance Win32_Process -Filter "ProcessId = $oldPid" -ErrorAction SilentlyContinue
    } else {
        $null
    }
    $oldCommandLine = [string]$oldProcess.CommandLine
    if ($oldProcess -and $oldCommandLine.IndexOf($releaseRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        Write-Host "Existing ROS 2 launch found (PID $oldPid); stopping it before restart..." -ForegroundColor Yellow
        & (Join-Path $releaseRoot "stop_robot.ps1")
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to stop the existing ROS 2 stack (PID $oldPid)."
        }
    } elseif ($oldProcess) {
        Write-Host "Ignoring stale run.pid $oldPid because it belongs to another process." -ForegroundColor Yellow
    } else {
        Write-Host "Removing stale run.pid $oldPid." -ForegroundColor Yellow
    }
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
}

$env:ROS_DOMAIN_ID = "0"
$env:RMW_IMPLEMENTATION = "rmw_fastrtps_cpp"
$env:ROS_LOCALHOST_ONLY = "1"
$env:ROS_AUTOMATIC_DISCOVERY_RANGE = "LOCALHOST"
$env:CRAWLING_ROBOT_STACK_LAUNCHER = "1"
$env:COLCON_PYTHON_EXECUTABLE = $python
$pythonRoot = Split-Path -Parent $python
$env:PATH = "$runtimeRoot;$pythonRoot;$pythonRoot\Scripts;$pythonRoot\Library\bin;$runtimeRoot\bin;$runtimeRoot\Library\bin;$env:PATH"

$monitorArguments = "--ros-args --params-file `"$configPath`""
$monitorCommand = "start `"Crawling Robot Drive Test`" /D `"$releaseRoot`" `"$env:ComSpec`" /d /c call `"$monitorLauncher`" $monitorArguments"
$launchArguments = "crawling_robot_bringup drive_test.launch.py parameters_file:=`"$configPath`""
if ($canPort) { $launchArguments += " can_serial_port:=$canPort" }
$command = "call `"$runtimeSetup`" && call `"$overlaySetup`" && $monitorCommand && `"$python`" `"$ros2Script`" launch $launchArguments"

Write-Host "Starting local crawling robot drive test..." -ForegroundColor Cyan
Write-Host "CAN: auto-detect USB-CAN, 1 Mbps"
Write-Host "Linear key speed: 0.005 m/s"
Write-Host "Turn key speed: 0.03 rad/s"
Write-Host "Click Drive Enable before using arrow keys. Space stops immediately."

$myPid = $PID
Set-Content -LiteralPath $pidFile -Value $myPid -Encoding ASCII
try {
    & $env:ComSpec /d /s /c $command
    exit $LASTEXITCODE
} finally {
    $recordedPid = ""
    try { $recordedPid = (Get-Content -Raw -LiteralPath $pidFile).Trim() } catch {}
    if ($recordedPid -eq "$myPid") {
        Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    }
}
