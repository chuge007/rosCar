[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$shortcutName = 'PA1664 Operator Workbench.lnk'
$startupFolder = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Startup)
$desktopFolder = [Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
$paths = @(
    (Join-Path $startupFolder $shortcutName),
    (Join-Path $desktopFolder $shortcutName)
)

foreach ($path in $paths) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Remove-Item -LiteralPath $path -Force
        Write-Host ('Removed: ' + $path)
    }
    else {
        Write-Host ('Not installed: ' + $path)
    }
}

Write-Host 'Autostart shortcuts removed. The running Workbench process was not stopped.'
exit 0