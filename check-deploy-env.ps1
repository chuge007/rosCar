$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "====================================================================" -ForegroundColor Cyan
Write-Host " PA1664 Deployment Environment Check" -ForegroundColor Cyan
Write-Host "====================================================================" -ForegroundColor Cyan

$allOk = $true

# Check project files
Write-Host ""
Write-Host "[1] Project files..." -ForegroundColor Yellow
$proFiles = @('PA1664RobotHost.pro', 'PA1664Workbench.pro')
foreach ($pro in $proFiles) {
    $path = Join-Path $projectRoot $pro
    if (Test-Path $path) {
        Write-Host "  [OK] $pro" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $pro" -ForegroundColor Red
        $allOk = $false
    }
}

# Check compiled executables
Write-Host ""
Write-Host "[2] Compiled executables..." -ForegroundColor Yellow
$binDir = Join-Path $projectRoot 'bin'
$exeFiles = @('PA1664RobotHost.exe', 'PA1664Workbench.exe')
foreach ($exe in $exeFiles) {
    $path = Join-Path $binDir $exe
    if (Test-Path $path) {
        $sizeMB = [Math]::Round((Get-Item $path).Length / 1MB, 2)
        Write-Host "  [OK] $exe - $sizeMB MB" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $exe - not compiled" -ForegroundColor Red
        $allOk = $false
    }
}

# Check runtime dependencies
Write-Host ""
Write-Host "[3] Runtime dependencies..." -ForegroundColor Yellow
$criticalDlls = @('Qt6Core.dll', 'Qt6Network.dll', 'opencv_world500.dll', 'client.dll')
foreach ($dll in $criticalDlls) {
    $path = Join-Path $binDir $dll
    if (Test-Path $path) {
        Write-Host "  [OK] $dll" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $dll" -ForegroundColor Red
        $allOk = $false
    }
}

# Check deployment packages
Write-Host ""
Write-Host "[4] Deployment packages..." -ForegroundColor Yellow
$startupDir = Join-Path $projectRoot 'startup_deploy'
$hostAppExe = Join-Path $startupDir 'robot_host\app\PA1664RobotHost.exe'
$workbenchAppExe = Join-Path $startupDir 'operator_workbench\app\PA1664Workbench.exe'

if (Test-Path $hostAppExe) {
    $sizeMB = [Math]::Round((Get-Item $hostAppExe).Length / 1MB, 2)
    Write-Host "  [OK] robot_host packaged - $sizeMB MB" -ForegroundColor Green
} else {
    Write-Host "  [WARN] robot_host not packaged yet" -ForegroundColor Yellow
}

if (Test-Path $workbenchAppExe) {
    $sizeMB = [Math]::Round((Get-Item $workbenchAppExe).Length / 1MB, 2)
    Write-Host "  [OK] operator_workbench packaged - $sizeMB MB" -ForegroundColor Green
} else {
    Write-Host "  [WARN] operator_workbench not packaged yet" -ForegroundColor Yellow
}

# Summary
Write-Host ""
Write-Host "====================================================================" -ForegroundColor Cyan
if ($allOk) {
    Write-Host " Environment check PASSED" -ForegroundColor Green
    if (-not (Test-Path $hostAppExe) -or -not (Test-Path $workbenchAppExe)) {
        Write-Host ""
        Write-Host " Next step: Run deploy-to-startup.ps1 to package" -ForegroundColor White
    } else {
        Write-Host ""
        Write-Host " Ready to deploy! See DEPLOY_GUIDE.md" -ForegroundColor White
    }
} else {
    Write-Host " Environment check FAILED" -ForegroundColor Red
}
Write-Host "====================================================================" -ForegroundColor Cyan
