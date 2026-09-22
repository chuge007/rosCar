$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $projectRoot 'build'
$vcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
$qmake = 'D:\qt\5.12.4\msvc2017_64\bin\qmake.exe'
$deploy = 'D:\qt\5.12.4\msvc2017_64\bin\windeployqt.exe'
$laserRuntime = 'D:\dev\deskCrawlingRobot\modules\mv3dlp_laser_profile\windows_x64\bin'
# OpenCV's vc16 DLL needs the newer unified VC runtime, including
# vcruntime140_1.dll. Do not overwrite it with the old VS2017 private CRT.
$vcRuntime = Join-Path $env:WINDIR 'System32'
$vcRuntimeFiles = @('msvcp140.dll', 'concrt140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
foreach ($runtimeFile in $vcRuntimeFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $vcRuntime $runtimeFile))) {
        throw "Install the current Microsoft Visual C++ x64 redistributable: missing $runtimeFile"
    }
}

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
    foreach ($runtimeFile in $vcRuntimeFiles) {
        Copy-Item -LiteralPath (Join-Path $vcRuntime $runtimeFile) -Destination (Join-Path $buildRoot 'release') -Force
    }
} finally {
    Pop-Location
}
