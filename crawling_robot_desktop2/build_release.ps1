$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $projectRoot 'build'
$vcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
$qmake = 'D:\qt\5.12.4\msvc2017_64\bin\qmake.exe'
$deploy = 'D:\qt\5.12.4\msvc2017_64\bin\windeployqt.exe'
$laserRuntime = Join-Path (Split-Path -Parent $projectRoot) 'modules\mv3dlp_laser_profile\windows_x64\bin'
$vcRuntime = 'C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Redist\MSVC\14.16.27012\x64\Microsoft.VC141.CRT'

if (!(Test-Path -LiteralPath $vcVars) -or !(Test-Path -LiteralPath $qmake)) {
    throw 'Qt 5.12.4/MSVC2017 x64 toolchain was not found at the configured paths.'
}

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
Push-Location $buildRoot
try {
    # Always remove stale objects so source-encoding changes are included in the release binary.
    $buildCommand = 'call "' + $vcVars + '" && nmake clean && "' + $qmake + '" ..\CrawlingRobotDesktop.pro -spec win32-msvc && nmake'
    cmd /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
    & $deploy --release (Join-Path $buildRoot 'release\CrawlingRobotDesktop.exe')
    if ($LASTEXITCODE -ne 0) {
        throw "Qt runtime deployment failed with exit code $LASTEXITCODE."
    }
    $laserTarget = Join-Path $buildRoot 'release\mv3dlp_sdk'
    & robocopy $laserRuntime $laserTarget /E /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -gt 7) {
        throw "MV3DLP SDK runtime deployment failed with exit code $LASTEXITCODE."
    }
    if (!(Test-Path -LiteralPath $vcRuntime)) {
        throw "MSVC x64 runtime was not found: $vcRuntime"
    }
    Get-ChildItem -LiteralPath $vcRuntime -File | Copy-Item -Destination (Join-Path $buildRoot 'release') -Force
} finally {
    Pop-Location
}
