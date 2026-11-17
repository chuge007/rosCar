param(
    [switch]$Fix
)
# Robot host network diagnostic / hardening script.
# Collects adapter, IP, route, power-management and event-log evidence,
# and with -Fix disables adapter power saving + USB selective suspend.
# Output: net_diag.txt next to this script.

$ErrorActionPreference = 'SilentlyContinue'
$out = Join-Path $PSScriptRoot 'net_diag.txt'
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
"=== robot host network diag  $stamp ===" | Out-File $out
"" | Out-File $out -Append

"[1] adapters" | Out-File $out -Append
Get-NetAdapter |
    Select-Object Name, InterfaceDescription, Status, LinkSpeed, MediaConnectionState,
                  MacAddress, DriverVersion, DriverDate, ifIndex |
    Format-Table -AutoSize | Out-File $out -Append

"[2] ipv4 addresses" | Out-File $out -Append
Get-NetIPAddress -AddressFamily IPv4 |
    Select-Object IPAddress, PrefixLength, InterfaceAlias, PrefixOrigin, SuffixOrigin |
    Format-Table -AutoSize | Out-File $out -Append

"[3] ip configuration" | Out-File $out -Append
Get-NetIPConfiguration | Format-List | Out-File $out -Append

"[4] routes" | Out-File $out -Append
Get-NetRoute -AddressFamily IPv4 |
    Select-Object DestinationPrefix, NextHop, InterfaceAlias, RouteMetric, ifMetric |
    Format-Table -AutoSize | Out-File $out -Append

"[5] adapter power management" | Out-File $out -Append
Get-NetAdapterPowerManagement | Format-List * | Out-File $out -Append

"[6] adapter advanced properties" | Out-File $out -Append
Get-NetAdapterAdvancedProperty |
    Select-Object Name, DisplayName, DisplayValue |
    Format-Table -AutoSize | Out-File $out -Append

"[7] adapter statistics" | Out-File $out -Append
Get-NetAdapterStatistics | Format-List * | Out-File $out -Append

"[8] system events (network, last 3 days)" | Out-File $out -Append
Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddDays(-3)} |
    Where-Object { $_.ProviderName -match 'Tcpip|NDIS|Dhcp|rtux|Realtek|USB|Kernel-PnP|netwtw|e1i' } |
    Select-Object -First 300 TimeCreated, ProviderName, Id,
                  @{n='Msg';e={($_.Message -split "`r`n")[0]}} |
    Format-Table -AutoSize -Wrap | Out-File $out -Append

"[9] arp table" | Out-File $out -Append
arp -a | Out-File $out -Append

if ($Fix) {
    "[10] applying hardening" | Out-File $out -Append
    foreach ($a in (Get-NetAdapter | Where-Object { $_.Status -ne 'Disabled' })) {
        "  adapter: $($a.Name)" | Out-File $out -Append
        Disable-NetAdapterPowerManagement -Name $a.Name -NoRestart | Out-Null
        foreach ($prop in (Get-NetAdapterAdvancedProperty -Name $a.Name)) {
            if ($prop.DisplayName -match 'Energy|Green|Eco|Wake|Magic|节能|环保') {
                $target = 'Disabled'
                if ($prop.ValidDisplayValues -contains 'Disabled') { $target = 'Disabled' }
                elseif ($prop.ValidDisplayValues -contains '关闭') { $target = '关闭' }
                if ($prop.DisplayValue -ne $target) {
                    Set-NetAdapterAdvancedProperty -Name $a.Name `
                        -DisplayName $prop.DisplayName -DisplayValue $target -NoRestart | Out-Null
                    "    $($prop.DisplayName): $($prop.DisplayValue) -> $target" | Out-File $out -Append
                }
            }
        }
    }
    # USB selective suspend: disable on both AC and DC
    $sub = '2a737441-1930-4402-8d77-b2bebba308a3'
    $set = '48e6b7a6-50f5-4782-a5d4-53bb8f07e226'
    powercfg /setacvalueindex SCHEME_CURRENT $sub $set 0 | Out-Null
    powercfg /setdcvalueindex SCHEME_CURRENT $sub $set 0 | Out-Null
    powercfg /setactive SCHEME_CURRENT | Out-Null
    "  USB selective suspend disabled" | Out-File $out -Append
    "  done. A reboot or adapter restart is required to apply." | Out-File $out -Append
}

"" | Out-File $out -Append
"=== end ===" | Out-File $out -Append
Write-Host "written: $out"
