[CmdletBinding()]
param(
    [string]$RosRoot = "",
    [string]$WorkspaceRoot = "",
    [string]$ReleaseRoot = "",
    [string]$TargetRoot = "",
    [string]$BuildBase = "",
    [string]$InstallBase = "",
    [string]$LogBase = "",
    [string]$CmakeGenerator = "Ninja",
    [string]$CxxCompiler = "",
    [string]$CCompiler = "",
    [string]$Package = "",
    [int]$ParallelWorkers = 0,
    [switch]$BuildTests,
    [switch]$Reconfigure,
    [switch]$NoBuild,
    [switch]$NoSync
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) { $WorkspaceRoot = Join-Path $repoRoot "ros2_ws" }
if ([string]::IsNullOrWhiteSpace($ReleaseRoot)) { $ReleaseRoot = Join-Path $repoRoot "target_board_release" }
if ([string]::IsNullOrWhiteSpace($RosRoot)) { $RosRoot = $env:ROS_DISTRO_ROOT }
if ([string]::IsNullOrWhiteSpace($RosRoot) -and (Test-Path -LiteralPath "D:\ros2\ros2-window")) {
    $RosRoot = "D:\ros2\ros2-window"
}
if ([string]::IsNullOrWhiteSpace($BuildBase)) { $BuildBase = Join-Path $ReleaseRoot "build" }
# Keep the incremental install overlay in the release tree. This is the
# artifact that is later copied to the target board over SSH.
if ([string]::IsNullOrWhiteSpace($InstallBase)) { $InstallBase = Join-Path $ReleaseRoot "install" }
if ([string]::IsNullOrWhiteSpace($LogBase)) { $LogBase = Join-Path $repoRoot "tmp\incremental_log" }
if ([string]::IsNullOrWhiteSpace($TargetRoot)) { $TargetRoot = $ReleaseRoot }
if ([string]::IsNullOrWhiteSpace($CxxCompiler)) {
    $defaultClang = "D:\androidSdk\ndk\28.2.13676358\toolchains\llvm\prebuilt\windows-x86_64\bin\clang-cl.exe"
    if (Test-Path -LiteralPath $defaultClang) { $CxxCompiler = $defaultClang }
}
if ([string]::IsNullOrWhiteSpace($CCompiler) -and $CxxCompiler) { $CCompiler = $CxxCompiler }

function FullPath([string]$path) { return [IO.Path]::GetFullPath($path) }
function Require-Path([string]$path, [string]$description) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing $description`: $path" }
}
function Repair-RelocatableOverlay([string]$installPath) {
    $batPath = Join-Path $installPath "setup.bat"
    if (Test-Path -LiteralPath $batPath) {
        $text = Get-Content -Raw -LiteralPath $batPath
        $text = $text -replace '(?im)^\s*call:_colcon_prefix_chain_bat_call_script "[A-Za-z]:\\[^\"]+\\local_setup\.bat"\s*$', 'call:_colcon_prefix_chain_bat_call_script "%%~dp0..\runtime\local_setup.bat"'
        Set-Content -LiteralPath $batPath -Value $text -Encoding ASCII
    }
    $psPath = Join-Path $installPath "setup.ps1"
    if (Test-Path -LiteralPath $psPath) {
        $text = Get-Content -Raw -LiteralPath $psPath
        $text = $text -replace '(?im)^\s*_colcon_prefix_chain_powershell_source_script "[A-Za-z]:\\[^\"]+\\local_setup\.ps1"\s*$', { '_colcon_prefix_chain_powershell_source_script "$PSScriptRoot\..\runtime\local_setup.ps1"' }
        Set-Content -LiteralPath $psPath -Value $text -Encoding UTF8
    }
    $localBatPath = Join-Path $installPath "local_setup.bat"
    if (Test-Path -LiteralPath $localBatPath) {
        $text = Get-Content -Raw -LiteralPath $localBatPath
        $text = $text -replace '(?im)^\s*set "_colcon_python_executable=[^"]*python\.exe"\s*$', '    set "_colcon_python_executable=%~dp0..\runtime\python\python.exe"'
        Set-Content -LiteralPath $localBatPath -Value $text -Encoding ASCII
    }
    $localPsPath = Join-Path $installPath "local_setup.ps1"
    if (Test-Path -LiteralPath $localPsPath) {
        $text = Get-Content -Raw -LiteralPath $localPsPath
        $text = $text -replace '(?im)^\s*\$_colcon_python_executable="[^"]*python\.exe"\s*$', { '  $_colcon_python_executable=(Join-Path (Split-Path $PSCommandPath -Parent) "..\runtime\python\python.exe")' }
        Set-Content -LiteralPath $localPsPath -Value $text -Encoding UTF8
    }
    $markerPath = Join-Path $installPath "share\ament_index\resource_index\parent_prefix_path"
    if (Test-Path -LiteralPath $markerPath) {
        Get-ChildItem -LiteralPath $markerPath -File | Remove-Item -Force
    }
}

$WorkspaceRoot = FullPath $WorkspaceRoot
$ReleaseRoot = FullPath $ReleaseRoot
$TargetRoot = FullPath $TargetRoot
$BuildBase = FullPath $BuildBase
$InstallBase = FullPath $InstallBase
$LogBase = FullPath $LogBase
if (-not $NoBuild) {
    Require-Path $RosRoot "ROS runtime root"
    Require-Path (Join-Path $RosRoot "local_setup.bat") "ROS local_setup.bat"
    $pythonCandidates = @(
        (Join-Path $RosRoot "python.exe"),
        (Join-Path $RosRoot "Scripts\python.exe"),
        (Join-Path $RosRoot ".pixi\envs\default\python.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if (-not $pythonCandidates) { throw "No Python executable was found below '$RosRoot'." }
    $pythonExecutable = $pythonCandidates | Select-Object -First 1
    $pythonCmakePath = $pythonExecutable.Replace('\', '/')
    $env:COLCON_PYTHON_EXECUTABLE = $pythonExecutable
    $pixiEnvRoot = Join-Path $RosRoot ".pixi\envs\default"
    $pathEntries = @(
        (Join-Path $pixiEnvRoot "Library\bin"),
        (Join-Path $pixiEnvRoot "Scripts"),
        (Join-Path $pixiEnvRoot "bin"),
        (Join-Path $RosRoot "bin")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    $env:PATH = (($pathEntries -join ";") + ";" + $env:PATH)
    $env:PYTHONPATH = ((Join-Path $pixiEnvRoot "Lib\site-packages") + ";" + $env:PYTHONPATH)

    $devSetup = ""
    if ($CmakeGenerator -eq "Ninja" -and ($CxxCompiler -or $CCompiler)) {
        $vsCandidates = @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
            "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
        ) | Where-Object { Test-Path -LiteralPath $_ }
        if (-not $vsCandidates) { throw "clang-cl build requires vcvars64.bat." }
        $devSetup = 'call "' + ($vsCandidates | Select-Object -First 1) + '" && set "VisualStudioVersion=" && '
    }

    $cmakeArgs = @(
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_STANDARD=17",
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
        "-DBUILD_TESTING=$(if ($BuildTests) { 'ON' } else { 'OFF' })",
        "-DPython3_EXECUTABLE=$pythonCmakePath",
        "-DPython3_FIND_STRATEGY=LOCATION"
    )
    if ($CmakeGenerator) { $cmakeArgs += @("-G", $CmakeGenerator) }
    if ($CxxCompiler) { $cmakeArgs += "-DCMAKE_CXX_COMPILER=$($CxxCompiler.Replace('\', '/'))" }
    if ($CCompiler) { $cmakeArgs += "-DCMAKE_C_COMPILER=$($CCompiler.Replace('\', '/'))" }
    $cmakeArgText = ($cmakeArgs | ForEach-Object { '"' + $_ + '"' }) -join " "
    $packageArgs = ""
    if (-not [string]::IsNullOrWhiteSpace($Package)) {
        $packageNames = ($Package -split ",") | ForEach-Object { $_.Trim() } | Where-Object { $_ }
        $packageArgs = " --packages-up-to " + (($packageNames | ForEach-Object { '"' + $_ + '"' }) -join " ")
    }
    $workerArgs = ""
    if ($ParallelWorkers -gt 0) {
        $workerArgs = " --parallel-workers $ParallelWorkers"
    }
    $cleanCacheArgs = ""
    if ($Reconfigure) {
        $cleanCacheArgs = " --cmake-clean-cache"
    } else {
        # A CMake cache keeps its original install prefix. When an existing
        # build tree is reused with a different overlay, generated headers
        # can be installed under the old prefix while dependents search the
        # new one. Detect that one-time migration and reconfigure only then.
        $expectedPrefix = $InstallBase.Replace('\', '/')
        $cacheFiles = Get-ChildItem -LiteralPath $BuildBase -Recurse -Filter "CMakeCache.txt" -File -ErrorAction SilentlyContinue
        foreach ($cache in $cacheFiles) {
            $prefixLine = Select-String -LiteralPath $cache.FullName -Pattern '^CMAKE_INSTALL_PREFIX:PATH=(.+)$' -SimpleMatch:$false |
                Select-Object -First 1
            if ($prefixLine -and $prefixLine.Matches[0].Groups[1].Value.Replace('\', '/') -ne $expectedPrefix) {
                $cleanCacheArgs = " --cmake-clean-cache"
                Write-Host "CMake install prefix changed; reconfiguring affected cache(s)." -ForegroundColor Yellow
                break
            }
        }
    }
    $buildCommand = "call `"$(Join-Path $RosRoot 'local_setup.bat')`" && colcon --log-base `"$LogBase`" build --merge-install --base-paths src --build-base `"$BuildBase`" --install-base `"$InstallBase`"$packageArgs$workerArgs$cleanCacheArgs --cmake-args $cmakeArgText"
    Write-Host "Incremental install output: $InstallBase" -ForegroundColor DarkGray
    Write-Host "Incremental build: $buildCommand" -ForegroundColor DarkGray
    Push-Location $WorkspaceRoot
    try {
        & $env:ComSpec /d /s /c ($devSetup + $buildCommand)
        if ($LASTEXITCODE -ne 0) { throw "Incremental ROS 2 build failed (exit $LASTEXITCODE). See $LogBase." }
    } finally {
        Pop-Location
    }
}

Require-Path (Join-Path $InstallBase "lib") "incremental install output"
if (-not $NoSync) {
    Require-Path $TargetRoot "target/release directory"
    Require-Path (Join-Path $TargetRoot "runtime") "bundled runtime in target/release directory"
    $destinationInstall = Join-Path $TargetRoot "install"
    New-Item -ItemType Directory -Force -Path $destinationInstall | Out-Null
    if ([IO.Path]::GetFullPath($InstallBase) -ne [IO.Path]::GetFullPath($destinationInstall)) {
        & robocopy.exe $InstallBase $destinationInstall /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS | Out-Null
        if ($LASTEXITCODE -gt 7) { throw "Failed to sync install overlay (robocopy exit $LASTEXITCODE)." }
    } else {
        Write-Host "Install overlay is already the target overlay; skipping duplicate copy." -ForegroundColor DarkGray
    }
    Repair-RelocatableOverlay $destinationInstall
    $releaseFiles = @(
        "run_robot.ps1",
        "run_drive_test.ps1",
        "run_drive_test.cmd",
        "stop_robot.ps1",
        "start_monitor.cmd"
    )
    foreach ($releaseFile in $releaseFiles) {
        $sourcePath = Join-Path $repoRoot "target_board_release\$releaseFile"
        if (-not (Test-Path -LiteralPath $sourcePath)) {
            throw "Required release launcher file is missing: $sourcePath"
        }
        $destinationPath = Join-Path $TargetRoot $releaseFile
        if ([IO.Path]::GetFullPath($sourcePath) -ne [IO.Path]::GetFullPath($destinationPath)) {
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        }
    }
    $targetConfig = Join-Path $TargetRoot "config"
    New-Item -ItemType Directory -Force -Path $targetConfig | Out-Null
    foreach ($configFile in @("robot.yaml", "drive_test.yaml", "fastdds_profile.xml")) {
        $sourcePath = Join-Path $repoRoot "target_board_release\config\$configFile"
        if (-not (Test-Path -LiteralPath $sourcePath)) {
            throw "Required release configuration file is missing: $sourcePath"
        }
        $destinationPath = Join-Path $targetConfig $configFile
        if ([IO.Path]::GetFullPath($sourcePath) -ne [IO.Path]::GetFullPath($destinationPath)) {
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        }
    }
    Write-Host "Updated target overlay: $destinationInstall" -ForegroundColor Green
}
Write-Host "Incremental update complete. Runtime and ZIP were not rebuilt." -ForegroundColor Green
