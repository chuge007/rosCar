$ErrorActionPreference = 'SilentlyContinue'
$target = '192.168.66.10'
$port = 43100

Write-Host "=============================================="
Write-Host " PA-1664 Workbench <-> Robot Host link check"
Write-Host " target: $target : $port"
Write-Host "=============================================="

Write-Host ""
Write-Host "[1] Local IPv4 addresses"
Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -ne '127.0.0.1' } |
    Select-Object IPAddress, PrefixLength, InterfaceAlias |
    Format-Table -AutoSize

Write-Host "[2] Local address inside target subnet"
$targetBytes = [System.Net.IPAddress]::Parse($target).GetAddressBytes()
$sameSubnet = @()
foreach ($entry in (Get-NetIPAddress -AddressFamily IPv4)) {
    if ($entry.IPAddress -eq '127.0.0.1' -or $entry.PrefixLength -le 0) { continue }
    $b = [System.Net.IPAddress]::Parse($entry.IPAddress).GetAddressBytes()
    $full = [math]::Floor($entry.PrefixLength / 8)
    $rem = $entry.PrefixLength % 8
    $same = $true
    for ($i = 0; $i -lt $full; $i++) {
        if ($b[$i] -ne $targetBytes[$i]) { $same = $false }
    }
    if ($rem -gt 0) {
        $mask = 256 - [math]::Pow(2, 8 - $rem)
        if (($b[$full] -band $mask) -ne ($targetBytes[$full] -band $mask)) { $same = $false }
    }
    if ($same) { $sameSubnet += $entry.IPAddress }
}
if ($sameSubnet.Count -gt 0) {
    Write-Host ("  OK  same subnet: " + ($sameSubnet -join ', ')) -ForegroundColor Green
} else {
    Write-Host "  NG  no local address inside $target subnet" -ForegroundColor Red
    Write-Host "      -> USB NIC unplugged / cable loose / adapter disabled" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[3] Ping target"
if (Test-Connection -ComputerName $target -Count 3 -Quiet) {
    Write-Host "  OK  target replies to ping" -ForegroundColor Green
} else {
    Write-Host "  NG  target unreachable (link down or host powered off)" -ForegroundColor Red
}

Write-Host ""
Write-Host "[4] Control port $port"
$tcp = Test-NetConnection -ComputerName $target -Port $port -WarningAction SilentlyContinue
if ($tcp.TcpTestSucceeded) {
    Write-Host "  OK  port reachable" -ForegroundColor Green
} else {
    Write-Host "  NG  port unreachable (host not running / firewall)" -ForegroundColor Red
}

Write-Host ""
Write-Host "[5] Robot host process on target"
if (Get-Command ssh -ErrorAction SilentlyContinue) {
    $sshOut = & ssh -o ConnectTimeout=6 -o BatchMode=yes "a@$target" 'chcp 65001 >nul & tasklist | findstr /I PA1664RobotHost' 2>&1
    if ($sshOut -match 'PA1664RobotHost') {
        Write-Host "  OK  PA1664RobotHost is running" -ForegroundColor Green
        Write-Host ("      " + ($sshOut -join ' '))
    } else {
        Write-Host "  NG  PA1664RobotHost not found (or ssh unavailable)" -ForegroundColor Red
    }
} else {
    Write-Host "  SKIP no ssh client" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[6] Running Workbench instances (only one allowed)"
$procs = @(Get-CimInstance Win32_Process -Filter "Name='PA1664Workbench.exe'")
if ($procs.Count -gt 1) {
    Write-Host ("  NG  " + $procs.Count + " instances running, they will fight for the control session") -ForegroundColor Red
    foreach ($p in $procs) {
        Write-Host ("      PID " + $p.ProcessId + "  " + $p.ExecutablePath)
    }
} elseif ($procs.Count -eq 1) {
    Write-Host ("  OK  1 instance, PID " + $procs[0].ProcessId) -ForegroundColor Green
} else {
    Write-Host "  --  no Workbench running"
}

Write-Host ""
Write-Host "--------------------------------------------------------------"
Write-Host "How to read this:"
Write-Host "  [2] NG                -> physical link problem, software cannot help"
Write-Host "  [2][3] OK, [4] NG     -> robot host not started"
Write-Host "  [6] NG                -> close extra operator windows, keep one"
Write-Host "  [4] OK but no data    -> search bin\logs\robot_console.log for"
Write-Host "                           LINK.CONTROL and not_session_owner"
Write-Host "--------------------------------------------------------------"
