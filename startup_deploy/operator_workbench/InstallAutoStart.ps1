[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$startScript = Join-Path $root 'StartOperatorWorkbench.ps1'
$appExe = Join-Path $root 'app\PA1664Workbench.exe'
$configPath = Join-Path $root 'config\workbench.psd1'
$shortcutName = 'PA1664 Operator Workbench.lnk'
$powerShellExe = Join-Path $PSHOME 'powershell.exe'

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
if ([string]::IsNullOrWhiteSpace([string]$config.Host)) {
    throw 'Set Host to the robot-car computer IP in config\workbench.psd1.'
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