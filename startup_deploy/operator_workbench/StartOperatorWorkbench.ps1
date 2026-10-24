[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$appDir = Join-Path $root 'app'
$configPath = Join-Path $root 'config\workbench.psd1'
$runtimeDir = Join-Path $root 'runtime'
$launcherLog = Join-Path $runtimeDir 'launcher.log'
$exePath = Join-Path $appDir 'PA1664Workbench.exe'

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
    $hostName = ([string]$config.Host).Trim()
    if ([string]::IsNullOrWhiteSpace($hostName)) {
        throw 'Host must not be empty in config\workbench.psd1.'
    }
    if ($hostName -match '\s') {
        throw 'Host must be an IPv4 address or DNS host name without spaces.'
    }
    $controlPort = Convert-ToPort $config.ControlPort 'ControlPort'
    $endpoint = $hostName + ':' + [string]$controlPort

    $running = @(Get-Process -Name 'PA1664Workbench' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Write-LauncherLog ('ALREADY_RUNNING pid=' + $running[0].Id)
        exit 0
    }

    $process = Start-Process -FilePath $exePath `
        -ArgumentList @('--remote', $endpoint) `
        -WorkingDirectory $appDir -PassThru
    Start-Sleep -Milliseconds 1500
    $process.Refresh()
    if ($process.HasExited) {
        throw ('PA1664Workbench exited during startup with code {0}. Check app\logs.' -f $process.ExitCode)
    }

    Write-LauncherLog ('STARTED pid={0} endpoint={1}' -f $process.Id, $endpoint)
    exit 0
}
catch {
    Stop-Launcher $_.Exception.Message
}