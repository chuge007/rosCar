$stop = @'
Get-Process -Name PA1664RobotHost -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
if (Test-Path C:\Users\a\Desktop\robot_host\app\robot_host.lock) { Remove-Item C:\Users\a\Desktop\robot_host\app\robot_host.lock -Force }
"stopped"
'@
$b64 = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($stop))
ssh a@192.168.1.20 powershell -NoProfile -EncodedCommand $b64
powershell -NoProfile -ExecutionPolicy Bypass -File d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmpSeparate\.codebuddy\start_remote_host.ps1
