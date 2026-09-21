[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$releaseRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$pidFile = Join-Path $releaseRoot "run.pid"

$processes = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
$protectedPids = New-Object System.Collections.Generic.HashSet[int]
$protectedPid = [int]$PID
while ($protectedPid -gt 0 -and $protectedPids.Add($protectedPid)) {
    $protectedProcess = $processes | Where-Object { [int]$_.ProcessId -eq $protectedPid } | Select-Object -First 1
    $protectedPid = if ($protectedProcess) { [int]$protectedProcess.ParentProcessId } else { 0 }
}

$pidsToStop = New-Object System.Collections.Generic.HashSet[int]
if (Test-Path -LiteralPath $pidFile) {
    try {
        $launchPid = [int](Get-Content -Raw -LiteralPath $pidFile)
        $launchProcess = $processes | Where-Object { [int]$_.ProcessId -eq $launchPid } | Select-Object -First 1
        $launchCommandLine = [string]$launchProcess.CommandLine
        if ($launchProcess -and
            -not $protectedPids.Contains($launchPid) -and
            $launchCommandLine.IndexOf($releaseRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            [void]$pidsToStop.Add($launchPid)
        }
    } catch {
        Write-Host "PID file is invalid; continuing with release-process cleanup." -ForegroundColor Yellow
    }
}

# Also find orphaned launchers, monitor, and nodes by their absolute release path.
# This covers Ctrl+C, a crashed launcher, and a deleted/stale run.pid.
foreach ($process in $processes) {
    $processId = [int]$process.ProcessId
    if ($protectedPids.Contains($processId)) { continue }
    $commandLine = [string]$process.CommandLine
    $executablePath = [string]$process.ExecutablePath
    if (($commandLine -and $commandLine.IndexOf($releaseRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) -or
        ($executablePath -and $executablePath.StartsWith($releaseRoot, [StringComparison]::OrdinalIgnoreCase))) {
        [void]$pidsToStop.Add($processId)
    }
}

function Get-ReleaseProcesses {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $processId = [int]$_.ProcessId
        $commandLine = [string]$_.CommandLine
        $executablePath = [string]$_.ExecutablePath
        -not $protectedPids.Contains($processId) -and
        (($commandLine -and $commandLine.IndexOf($releaseRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) -or
         ($executablePath -and $executablePath.StartsWith($releaseRoot, [StringComparison]::OrdinalIgnoreCase)))
    }
}

if ($pidsToStop.Count -eq 0) {
    Write-Host "No Crawling Robot ROS 2 processes are running."
} else {
    # Stop the ros2 launch parent first so launch_ros can request its nodes to
    # shut down through the normal ROS 2 process group.
    $launchParent = Get-ReleaseProcesses | Where-Object {
        $_.Name -ieq "python.exe" -and ([string]$_.CommandLine).Contains("ros2-script.py") -and
        ([string]$_.CommandLine).Contains(" launch ")
    } | Select-Object -First 1
    if ($launchParent) {
        [void]$pidsToStop.Add([int]$launchParent.ProcessId)
        try {
            $taskkillOutput = & taskkill.exe /PID ([int]$launchParent.ProcessId) 2>&1
            if ($LASTEXITCODE -eq 0) { $taskkillOutput | Out-Host }
        } catch {}
    }

    # Give launch_ros time to propagate shutdown and let every node release
    # hardware handles (especially the MV3D camera).
    Start-Sleep -Seconds 5
    $remaining = @(Get-ReleaseProcesses)
    foreach ($process in $remaining) {
        try {
            $taskkillOutput = & taskkill.exe /PID ([int]$process.ProcessId) /T 2>&1
            if ($LASTEXITCODE -eq 0) { $taskkillOutput | Out-Host }
        } catch {}
    }
    Start-Sleep -Seconds 3

    # A crashed launch or a console process can ignore the graceful request.
    # Only the remaining processes under this release directory are forced.
    $remaining = @(Get-ReleaseProcesses)
    foreach ($process in $remaining) {
        try {
            $taskkillOutput = & taskkill.exe /PID ([int]$process.ProcessId) /T /F 2>&1
            if ($LASTEXITCODE -eq 0) { $taskkillOutput | Out-Host }
        } catch {}
    }
    Start-Sleep -Seconds 1
    $leftovers = @(Get-ReleaseProcesses)
    if ($leftovers.Count -gt 0) {
        Write-Host "[ERROR] ROS 2 release processes remain after stop:" -ForegroundColor Red
        $leftovers | Select-Object Name,ProcessId,CommandLine | Format-Table -Wrap | Out-Host
        exit 1
    }
    Write-Host "Crawling Robot ROS 2 processes stopped."
}

# The MV3D camera debug application can keep the camera handle after ROS has
# connected to the same device. It is not part of the ROS process tree, so
# clean this known application explicitly while leaving SDK log services alive.
$cameraDebugProcesses = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -ieq "3DMVS.exe" -or
        ([string]$_.ExecutablePath).StartsWith("${env:ProgramFiles(x86)}\3DMVS\", [StringComparison]::OrdinalIgnoreCase)
    }
foreach ($process in $cameraDebugProcesses) {
    try {
        $guiProcess = Get-Process -Id ([int]$process.ProcessId) -ErrorAction SilentlyContinue
        if ($guiProcess -and $guiProcess.MainWindowHandle -ne 0) {
            [void]$guiProcess.CloseMainWindow()
        } else {
            & taskkill.exe /PID ([int]$process.ProcessId) /T 2>&1 | Out-Host
        }
    } catch {
        # The application may already have exited while ROS was stopping.
    }
}
if ($cameraDebugProcesses) {
    Start-Sleep -Seconds 3
    $remainingCameraDebug = Get-Process -Name 3DMVS -ErrorAction SilentlyContinue
    if ($remainingCameraDebug) {
        Write-Host "[WARN] 3DMVS.exe is still running; camera may remain occupied." -ForegroundColor Yellow
        foreach ($process in $remainingCameraDebug) {
            try { & taskkill.exe /PID $process.Id /T /F 2>&1 | Out-Host } catch {}
        }
    } else {
        Write-Host "MV3D camera debug application stopped." -ForegroundColor Green
    }
}

Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
$global:LASTEXITCODE = 0
