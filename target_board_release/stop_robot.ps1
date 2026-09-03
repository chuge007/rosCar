[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$releaseRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$pidFile = Join-Path $releaseRoot "run.pid"

$pidsToStop = New-Object System.Collections.Generic.HashSet[int]
if (Test-Path -LiteralPath $pidFile) {
    try {
        $launchPid = [int](Get-Content -Raw -LiteralPath $pidFile)
        if ($launchPid -gt 0) { [void]$pidsToStop.Add($launchPid) }
    } catch {
        Write-Host "PID file is invalid; continuing with release-process cleanup." -ForegroundColor Yellow
    }
}

# Also find orphaned launchers, monitor, and nodes by their absolute release path.
# This covers Ctrl+C, a crashed launcher, and a deleted/stale run.pid.
$processes = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue
foreach ($process in $processes) {
    $commandLine = [string]$process.CommandLine
    $executablePath = [string]$process.ExecutablePath
    if (($commandLine -and $commandLine.IndexOf($releaseRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0) -or
        ($executablePath -and $executablePath.StartsWith($releaseRoot, [StringComparison]::OrdinalIgnoreCase))) {
        [void]$pidsToStop.Add([int]$process.ProcessId)
    }
}

if ($pidsToStop.Count -eq 0) {
    Write-Host "No Crawling Robot ROS 2 processes are running."
} else {
    foreach ($processId in @($pidsToStop)) {
        try {
            $taskkillOutput = & taskkill.exe /PID $processId /T /F 2>&1
            if ($LASTEXITCODE -eq 0) {
                $taskkillOutput | Out-Host
            }
        } catch {
            # Another process in the same tree may have already stopped it.
        }
    }
    Start-Sleep -Milliseconds 500
    Write-Host "Crawling Robot ROS 2 processes stopped."
}

Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
