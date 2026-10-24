[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$startScript = Join-Path $root 'StartOperatorWorkbench.ps1'
$appExe = Join-Path $root 'app\PA1664Workbench.exe'
$configPath = Join-Path $root 'config\workbench.psd1'
$shortcutName = 'PA1664 Operator Workbench.lnk'
$powerShellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (-not (Test-Path -LiteralPath $powerShellExe -PathType Leaf)) {
    $powerShellExe = Join-Path $PSHOME 'powershell.exe'
}
if (-not (Test-Path -LiteralPath $powerShellExe -PathType Leaf)) {
    throw 'Windows PowerShell executable was not found.'
}

if (-not (Test-Path -LiteralPath $startScript -PathType Leaf)) {
    throw ('Missing start script: ' + $startScript)
}
if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) {
    throw ('Deploy the complete Workbench runtime first. Missing: ' + $appExe)
}
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw ('Missing Workbench endpoint configuration: ' + $configPath)
}

$config = Import-PowerShellDataFile -Path $configPath
$hostName = ([string]$config.Host).Trim()
if ([string]::IsNullOrWhiteSpace($hostName)) {
    throw 'Set Host to the robot-car computer IP in config\workbench.psd1.'
}
if ($hostName -match '\s') {
    throw 'Host must be an IPv4 address or DNS host name without spaces.'
}
$controlPort = 0
if (-not [int]::TryParse([string]$config.ControlPort, [ref]$controlPort) -or
    $controlPort -lt 1 -or $controlPort -gt 65535) {
    throw 'ControlPort must be an integer from 1 to 65535.'
}

$startupFolder = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Startup)
$desktopFolder = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
if ([string]::IsNullOrWhiteSpace($startupFolder) -or
    [string]::IsNullOrWhiteSpace($desktopFolder)) {
    throw 'Unable to resolve the current user Startup or Desktop folder.'
}

function New-WorkbenchShortcut([string]$shortcutPath) {
    $shell = New-Object -ComObject WScript.Shell
    try {
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $powerShellExe
        $shortcut.Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $startScript + '"'
        $shortcut.WorkingDirectory = $root
        $shortcut.WindowStyle = 7
        $shortcut.IconLocation = $appExe + ',0'
        $shortcut.Description = 'Start the PA1664 operator workbench at user logon.'
        $shortcut.Save()
    }
    finally {
        if ($null -ne $shell) {
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
        }
    }
}

$startupShortcut = Join-Path $startupFolder $shortcutName
$desktopShortcut = Join-Path $desktopFolder $shortcutName
New-WorkbenchShortcut $startupShortcut
New-WorkbenchShortcut $desktopShortcut

Write-Host 'PA1664 Operator Workbench autostart installed.'
Write-Host ('Remote Host: ' + [string]$config.Host + ':' + [string]$config.ControlPort)
Write-Host ('Logon shortcut: ' + $startupShortcut)
Write-Host ('Desktop shortcut: ' + $desktopShortcut)
exit 0