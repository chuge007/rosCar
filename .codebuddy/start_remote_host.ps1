$script = @'
$cmd = "cmd /c cd /d C:\Users\a\Desktop\robot_host\app && PA1664RobotHost.exe --listen 0.0.0.0 --control-port 43100 --realtime-port 43101 --media-port 43102 --bulk-port 43103"
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd } | Select-Object ProcessId,ReturnValue
'@
$b64 = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
ssh a@192.168.1.20 powershell -NoProfile -EncodedCommand $b64
