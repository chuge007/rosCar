[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$startScript = Join-Path $root 'StartRobotHost.ps1'
$appExe = Join-Path $root 'app\PA1664RobotHost.exe'
$shortcutName = 'PA1664 Robot Host.lnk'
$powerShellExe = Join-Path $PSHOME 'powershell.exe'

if (-not (Test-Path -LiteralPath $startScript -PathType Leaf)) {
    throw ('Missing start script: ' + $startScript)
}
if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) {
    throw ('Deploy the complete Host runtime first. Missing: ' + $appExe)
}

$startupFolder = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Startup)
$desktopFolder = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
if ([string]::IsNullOrWhiteSpace($startupFolder) -or
    [string]::IsNullOrWhiteSpace($desktopFolder)) {
    throw 'Unable to resolve the current user Startup or Desktop folder.'
}

function New-HostShortcut([string]$shortcutPath) {
    $shell = New-Object -ComObject WScript.Shell
    try {
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $powerShellExe
        $shortcut.Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $startScript + '"'
        $shortcut.WorkingDirectory = $root
        $shortcut.WindowStyle = 7
        $shortcut.IconLocation = $appExe + ',0'
        $shortcut.Description = 'Start the PA1664 robot host at user logon.'
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
New-HostShortcut $startupShortcut
New-HostShortcut $desktopShortcut

Write-Host 'PA1664 Robot Host autostart installed.'
Write-Host ('Logon shortcut: ' + $startupShortcut)
Write-Host ('Desktop shortcut: ' + $desktopShortcut)
exit 0