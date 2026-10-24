[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$appDir = Join-Path $root 'app'
$configPath = Join-Path $root 'config\host.psd1'
$runtimeDir = Join-Path $root 'runtime'
$launcherLog = Join-Path $runtimeDir 'launcher.log'
$exePath = Join-Path $appDir 'PA1664RobotHost.exe'

New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null

function Write-LauncherLog([string]$message) {
    $line = '{0:yyyy-MM-dd HH:mm:ss.fff} {1}' -f (Get-Date), $message
    Add-Content -LiteralPath $launcherLog -Value $line -Encoding UTF8
}

function Stop-Launcher([string]$message) {
    Write-LauncherLog ('ERROR ' + $message)
    [Console]::Error.WriteLine($message)
    exit 1
}

function Convert-ToPort($value, [string]$name) {
    $port = 0
    if (-not [int]::TryParse([string]$value, [ref]$port) -or
        $port -lt 1 -or $port -gt 65535) {
        throw ("Invalid {0}: {1}" -f $name, $value)
    }
    return $port
}

try {
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        Stop-Launcher ('Missing configuration: ' + $configPath)
    }
    if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
        Stop-Launcher ('Missing executable: ' + $exePath)
    }

    $config = Import-PowerShellDataFile -Path $configPath
    $listenAddress = ([string]$config.ListenAddress).Trim()
    if ([string]::IsNullOrWhiteSpace($listenAddress)) {
        throw 'ListenAddress must not be empty.'
    }

    $controlPort = Convert-ToPort $config.ControlPort 'ControlPort'
    $realtimePort = Convert-ToPort $config.RealtimePort 'RealtimePort'
    $mediaPort = Convert-ToPort $config.MediaPort 'MediaPort'
    $bulkPort = Convert-ToPort $config.BulkPort 'BulkPort'
    $uniquePorts = @($controlPort, $realtimePort, $mediaPort, $bulkPort) |
        Select-Object -Unique
    if (@($uniquePorts).Count -ne 4) {
        throw 'ControlPort, RealtimePort, MediaPort and BulkPort must be different.'
    }

    $running = @(Get-Process -Name 'PA1664RobotHost' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Write-LauncherLog ('ALREADY_RUNNING pid=' + $running[0].Id)
        exit 0
    }

    $arguments = @(
        '--listen', $listenAddress,
        '--control-port', [string]$controlPort,
        '--realtime-port', [string]$realtimePort,
        '--media-port', [string]$mediaPort,
        '--bulk-port', [string]$bulkPort
    )

    $process = Start-Process -FilePath $exePath -ArgumentList $arguments `
        -WorkingDirectory $appDir -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 900
    $process.Refresh()
    if ($process.HasExited) {
        throw ('PA1664RobotHost exited during startup with code {0}. Check app\logs\robot_host.log.' -f $process.ExitCode)
    }

    Write-LauncherLog ('STARTED pid={0} endpoint={1}:{2}' -f $process.Id, $listenAddress, $controlPort)
    exit 0
}
catch {
    Stop-Launcher $_.Exception.Message
}