[CmdletBinding()]
param(
    [string]$Destination = ""
)

$releaseRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$destinationRoot = if ([string]::IsNullOrWhiteSpace($Destination)) {
    [Environment]::GetFolderPath("Desktop")
} else {
    (Resolve-Path $Destination).Path
}
$shortcutPath = Join-Path $destinationRoot "爬壁机器人 ROS 2.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $releaseRoot "run_robot.cmd"
$shortcut.WorkingDirectory = $releaseRoot
$shortcut.Description = "Start the crawling robot ROS 2 stack"
$shortcut.IconLocation = "$env:SystemRoot\System32\shell32.dll,137"
$shortcut.Save()
Write-Host "Shortcut created: $shortcutPath"
