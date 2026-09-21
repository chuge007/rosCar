[CmdletBinding()]
param(
    [string]$RosRoot = "",
    [string]$WorkspaceRoot = "",
    [string]$OutputRoot = "",
    [string]$VendorSdkRoot = "",
    [string]$CmakeGenerator = "",
    [string]$CmakeGeneratorPlatform = "",
    [string]$CxxCompiler = "",
    [string]$CCompiler = "",
    [string]$StagingRoot = "",
    [switch]$IncludeSource,
    [switch]$SkipBuild,
    [switch]$Zip,
    [switch]$Clean,
    [switch]$KeepStaging
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) { $WorkspaceRoot = Join-Path $repoRoot "ros2_ws" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $repoRoot "target_board_release" }
if ([string]::IsNullOrWhiteSpace($VendorSdkRoot)) {
    $VendorSdkRoot = Join-Path $repoRoot "modules\mv3dlp_laser_profile\windows_x64\bin"
}

function FullPath([string]$path) { return (Resolve-Path -LiteralPath $path).Path }
function Require-Path([string]$path, [string]$description) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing $description`: $path" }
}
function Copy-Tree([string]$source, [string]$destination) {
    Require-Path $source "source directory"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    & robocopy.exe $source $destination /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -gt 7) { throw "robocopy failed copying '$source' to '$destination' (exit $LASTEXITCODE)" }
}
function Copy-RosRuntime([string]$rosRoot, [string]$destination) {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    # The ROS prefix is the runtime; omit the Pixi cache and downloaded source
    # archive. The self-contained Pixi environment is copied separately below
    # because it supplies Python and the native DLLs used by launch/ros2cli.
    & robocopy.exe $rosRoot $destination /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /XD (Join-Path $rosRoot ".pixi") /XF "ros2-jazzy-*.zip" | Out-Null
    if ($LASTEXITCODE -gt 7) { throw "robocopy failed copying ROS runtime '$rosRoot' (exit $LASTEXITCODE)" }
    $envRoot = Join-Path $rosRoot ".pixi\envs\default"
    Require-Path (Join-Path $envRoot "python.exe") "ROS Python runtime"
    Copy-Tree $envRoot (Join-Path $destination "python")
}
function Repair-RelocatableOverlay([string]$installPath, [string]$originalWorkspace) {
    # colcon records every sourced parent prefix as an absolute path. The
    # release contains the ROS prefix beside the overlay, so replace those
    # generated paths with a path relative to the installed overlay.
    $batPath = Join-Path $installPath "setup.bat"
    if (Test-Path -LiteralPath $batPath) {
        $text = Get-Content -Raw -LiteralPath $batPath
        $batPattern = '(?im)^\s*call:_colcon_prefix_chain_bat_call_script "[A-Za-z]:\\[^\"]+\\local_setup\.bat"\s*$'
        $text = $text -replace $batPattern, 'call:_colcon_prefix_chain_bat_call_script "%%~dp0..\runtime\local_setup.bat"'
        Set-Content -LiteralPath $batPath -Value $text -Encoding ASCII
    }
    $psPath = Join-Path $installPath "setup.ps1"
    if (Test-Path -LiteralPath $psPath) {
        $text = Get-Content -Raw -LiteralPath $psPath
        $psPattern = '(?im)^\s*_colcon_prefix_chain_powershell_source_script "[A-Za-z]:\\[^\"]+\\local_setup\.ps1"\s*$'
        $text = $text -replace $psPattern, '_colcon_prefix_chain_powershell_source_script "$PSScriptRoot\..\runtime\local_setup.ps1"'
        Set-Content -LiteralPath $psPath -Value $text -Encoding UTF8
    }

    # Make direct calls to install\local_setup.* portable as well. The
    # launcher sets COLCON_PYTHON_EXECUTABLE, but users may source this file
    # manually while diagnosing a target-board installation.
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

    # These build-time markers are consumed while generating environment
    # hooks, not when the already-generated overlay is launched. Leaving the
    # original absolute paths here makes the copied package look nonportable
    # and can mislead tools that inspect the ament index.
    $markerPath = Join-Path $installPath "share\ament_index\resource_index\parent_prefix_path"
    if (Test-Path -LiteralPath $markerPath) {
        Get-ChildItem -LiteralPath $markerPath -File | Remove-Item -Force
    }
}

function Repair-BundledRuntime([string]$runtimePath) {
    # The copied Pixi environment has no stable install prefix. Keep the
    # generated fallback executable paths relative to runtime\local_setup.*.
    $batPath = Join-Path $runtimePath "local_setup.bat"
    if (Test-Path -LiteralPath $batPath) {
        $text = Get-Content -Raw -LiteralPath $batPath
        $text = $text -replace '(?im)^\s*set "_colcon_python_executable=[^"]*python\.exe"\s*$', '    set "_colcon_python_executable=%~dp0python\python.exe"'
        Set-Content -LiteralPath $batPath -Value $text -Encoding ASCII
    }
    $psPath = Join-Path $runtimePath "local_setup.ps1"
    if (Test-Path -LiteralPath $psPath) {
        $text = Get-Content -Raw -LiteralPath $psPath
        $text = $text -replace '(?im)^\s*\$_colcon_python_executable="[^"]*python\.exe"\s*$', { '  $_colcon_python_executable=(Join-Path (Split-Path $PSCommandPath -Parent) "python\python.exe")' }
        Set-Content -LiteralPath $psPath -Value $text -Encoding UTF8
    }
}
function Copy-MsvcRuntime([string]$destination) {
    $names = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "concrt140.dll")
    $roots = @()
    if ($env:VCToolsRedistDir) { $roots += $env:VCToolsRedistDir }
    if ($env:VCToolsInstallDir) { $roots += $env:VCToolsInstallDir }
    foreach ($name in $names) {
        $alreadyBundled = Get-ChildItem -LiteralPath $destination -Recurse -File -Filter $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($alreadyBundled) { continue }
        $found = $null
        foreach ($root in $roots) {
            if (Test-Path -LiteralPath $root) {
                $found = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match "(x64|amd64)" } | Select-Object -First 1
                if ($found) { break }
            }
        }
        if ($found) { Copy-Item -LiteralPath $found.FullName -Destination (Join-Path $destination $name) -Force }
    }
}

$WorkspaceRoot = FullPath $WorkspaceRoot
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$VendorSdkRoot = FullPath $VendorSdkRoot

if ([string]::IsNullOrWhiteSpace($RosRoot)) {
    $RosRoot = $env:ROS_DISTRO_ROOT
}
if (-not $SkipBuild -and [string]::IsNullOrWhiteSpace($RosRoot)) {
    throw "ROS runtime root is required. Pass -RosRoot (for example C:\pixi_ws\ros2-window)."
}
if (-not [string]::IsNullOrWhiteSpace($RosRoot)) {
    $RosRoot = FullPath $RosRoot
    Require-Path (Join-Path $RosRoot "setup.bat") "ROS setup.bat"
    Require-Path (Join-Path $RosRoot "local_setup.bat") "ROS local_setup.bat"
    $pythonCandidates = @(
        (Join-Path $RosRoot "python.exe"),
        (Join-Path $RosRoot "Scripts\python.exe"),
        (Join-Path $RosRoot ".pixi\envs\default\python.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if (-not $pythonCandidates) {
        throw "Bundled ROS runtime has no python.exe. A target board without Python installed cannot run the portable package."
    }
    if (-not $SkipBuild) {
        $ros2Candidates = @(
            (Join-Path $RosRoot "ros2.exe"),
            (Join-Path $RosRoot "Scripts\ros2.exe"),
            (Join-Path $RosRoot "bin\ros2.exe")
        )
        if (-not ($ros2Candidates | Where-Object { Test-Path -LiteralPath $_ })) {
            throw "ROS 2 command was not found below '$RosRoot'."
        }
        $pixiEnvRoot = Join-Path $RosRoot ".pixi\envs\default"
        $pixiPathEntries = @(
            (Join-Path $pixiEnvRoot "Library\bin"),
            (Join-Path $pixiEnvRoot "Scripts"),
            (Join-Path $pixiEnvRoot "bin"),
            (Join-Path $RosRoot "bin")
        ) | Where-Object { Test-Path -LiteralPath $_ }
        $env:PATH = (($pixiPathEntries -join ";") + ";" + $env:PATH)
        $env:COLCON_PYTHON_EXECUTABLE = ($pythonCandidates | Select-Object -First 1)
        $env:PYTHONPATH = ((Join-Path $pixiEnvRoot "Lib\site-packages") + ";" + (Join-Path $RosRoot "Lib\site-packages") + ";" + $env:PYTHONPATH)
        if ($CmakeGenerator) { $env:CMAKE_GENERATOR = $CmakeGenerator }
        if ($CmakeGeneratorPlatform) { $env:CMAKE_GENERATOR_PLATFORM = $CmakeGeneratorPlatform }
        if ($CmakeGenerator -match '^Visual Studio (\d+)') {
            $env:VisualStudioVersion = "$($Matches[1]).0"
        }
        if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw "cmake.exe is not on PATH. Load the Visual Studio developer environment first." }
        if (-not (Get-Command colcon.exe -ErrorAction SilentlyContinue)) { throw "colcon.exe is not on PATH." }
    }
}

if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
    $StagingRoot = Join-Path $repoRoot "tmp"
}
New-Item -ItemType Directory -Force -Path $StagingRoot | Out-Null
$staging = Join-Path ([IO.Path]::GetFullPath($StagingRoot)) ("crb_" + [guid]::NewGuid().ToString("N").Substring(0, 12))
$stagedInstall = Join-Path $staging "install"
$templateRoot = Join-Path $staging "template"
try {
    New-Item -ItemType Directory -Force -Path $staging | Out-Null
    # Snapshot launcher/config templates before -Clean removes the default
    # output directory (which is also the checked-in template directory).
    Copy-Tree (Join-Path $repoRoot "target_board_release\config") (Join-Path $templateRoot "config")
    foreach ($template in @("run_robot.ps1", "run_robot.cmd", "run_drive_test.ps1", "run_drive_test.cmd", "stop_robot.ps1", "stop_robot.cmd", "start_monitor.cmd", "create_shortcut.ps1", "create_shortcut.cmd", "README.md")) {
        Copy-Item -LiteralPath (Join-Path $repoRoot "target_board_release\$template") -Destination (Join-Path $templateRoot $template) -Force
    }

    if (-not $SkipBuild) {
        Write-Host "Building ROS 2 overlay..." -ForegroundColor Cyan
        $buildBase = Join-Path $staging "build"
        $logBase = Join-Path $staging "log"
        $pythonExecutable = ($pythonCandidates | Select-Object -First 1)
        $cmakeArgs = @(
            '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_CXX_STANDARD=17',
            '-DCMAKE_CXX_STANDARD_REQUIRED=ON',
            '-DBUILD_TESTING=OFF',
            "-DPython3_EXECUTABLE=$pythonExecutable",
            '-DPython3_FIND_STRATEGY=LOCATION'
        )
        if ($CmakeGenerator) {
            $cmakeArgs += @('-G', $CmakeGenerator)
            if ($CmakeGeneratorPlatform -and $CmakeGenerator -match 'Visual Studio') {
                $cmakeArgs += @('-A', $CmakeGeneratorPlatform)
            }
        }
        if ($CxxCompiler) {
            $cmakeArgs += "-DCMAKE_CXX_COMPILER=$($CxxCompiler.Replace('\', '/'))"
        }
        if ($CCompiler) {
            $cmakeArgs += "-DCMAKE_C_COMPILER=$($CCompiler.Replace('\', '/'))"
        }
        $cmakeArgText = ($cmakeArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $devSetup = ''
        if ($CmakeGenerator -eq 'Ninja' -and ($CxxCompiler -or $CCompiler)) {
            $vsCandidates = @(
                'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
                'C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
            ) | Where-Object { Test-Path -LiteralPath $_ }
            if (-not $vsCandidates) {
                throw 'clang-cl build requires a Visual Studio x64 SDK environment (vcvars64.bat was not found).'
            }
            $devSetup = 'call "' + ($vsCandidates | Select-Object -First 1) + '" && set "VisualStudioVersion=" && '
        }
        $buildCommand = "call `"$(Join-Path $RosRoot 'local_setup.bat')`" && colcon --log-base `"$logBase`" build --merge-install --build-base `"$buildBase`" --install-base `"$stagedInstall`" --cmake-args $cmakeArgText"
        $buildCommand = "$devSetup$buildCommand"
        Write-Host "Build command: $buildCommand" -ForegroundColor DarkGray
        Push-Location $WorkspaceRoot
        try {
            & $env:ComSpec /d /s /c $buildCommand
            if ($LASTEXITCODE -ne 0) { throw "ROS 2 workspace build failed (exit $LASTEXITCODE). See $logBase." }
        } finally {
            Pop-Location
        }
    } else {
        $existingInstall = Join-Path $WorkspaceRoot "install"
        Copy-Tree $existingInstall $stagedInstall
    }

    $required = @(
        "lib\crawling_robot_drivers\base_drive_node.exe",
        "lib\crawling_robot_drivers\rim302_imu_node.exe",
        "lib\crawling_robot_drivers\modbus_encoder_node.exe",
        "lib\crawling_robot_drivers\encoder_odom_node.exe",
        "lib\crawling_robot_drivers\canopen_axis_node.exe",
        "lib\crawling_robot_laser\mv3dlp_laser_node.exe",
        "lib\crawling_robot_control\laser_path_follower_node.exe",
        "lib\crawling_robot_monitor\robot_monitor.exe"
    )
    foreach ($relative in $required) { Require-Path (Join-Path $stagedInstall $relative) "built executable" }
    Require-Path (Join-Path $VendorSdkRoot "Mv3dLp.dll") "MV3DLP SDK"

    if ($Clean -and (Test-Path -LiteralPath $OutputRoot)) {
        Remove-Item -LiteralPath $OutputRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    Copy-Tree $stagedInstall (Join-Path $OutputRoot "install")
    Copy-Tree $VendorSdkRoot (Join-Path $OutputRoot "vendor\mv3dlp_sdk")
    Copy-Tree (Join-Path $templateRoot "config") (Join-Path $OutputRoot "config")
    if (-not [string]::IsNullOrWhiteSpace($RosRoot)) {
        Copy-RosRuntime $RosRoot (Join-Path $OutputRoot "runtime")
    }
    if (-not (Test-Path -LiteralPath (Join-Path $OutputRoot "runtime\local_setup.bat")) -and
        -not (Test-Path -LiteralPath (Join-Path $OutputRoot "runtime\setup.bat"))) {
        throw "Packaged ROS runtime has neither local_setup.bat nor setup.bat."
    }
    Repair-BundledRuntime (Join-Path $OutputRoot "runtime")
    Copy-MsvcRuntime (Join-Path $OutputRoot "runtime")
    Repair-RelocatableOverlay (Join-Path $OutputRoot "install") $WorkspaceRoot
    if ($IncludeSource) {
        Copy-Tree (Join-Path $WorkspaceRoot "src") (Join-Path $OutputRoot "source\ros2_ws\src")
        Copy-Tree (Join-Path $repoRoot "modules\mv3dlp_laser_profile") (Join-Path $OutputRoot "source\modules\mv3dlp_laser_profile")
    }

    foreach ($template in @("run_robot.ps1", "run_robot.cmd", "run_drive_test.ps1", "run_drive_test.cmd", "stop_robot.ps1", "stop_robot.cmd", "start_monitor.cmd", "create_shortcut.ps1", "create_shortcut.cmd", "README.md")) {
        Copy-Item -LiteralPath (Join-Path $templateRoot $template) -Destination (Join-Path $OutputRoot $template) -Force
    }

    $manifest = [ordered]@{
        product = "crawling_robot_ros2"
        platform = "windows-x64"
        built_at_utc = [DateTime]::UtcNow.ToString("o")
        # Keep machine-specific source paths out of the portable manifest.
        ros_root_source = "bundled-runtime"
        packages = @("crawling_robot_bringup", "crawling_robot_drivers", "crawling_robot_interfaces", "crawling_robot_laser", "crawling_robot_control", "crawling_robot_monitor")
        start = "run_robot.cmd"
        stop = "stop_robot.cmd"
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputRoot "manifest.json") -Encoding UTF8

    if ($Zip) {
        $zipPath = "$OutputRoot.zip"
        if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
        # Compress-Archive creates an empty/truncated archive for multi-GB
        # trees on some Windows PowerShell/.NET combinations. Windows ships
        # bsdtar, which writes ZIP64 archives and keeps the release directory
        # as the archive's top-level folder.
        $zipParent = Split-Path -Parent $OutputRoot
        $zipLeaf = Split-Path -Leaf $OutputRoot
        & tar.exe -a -c -f $zipPath -C $zipParent $zipLeaf
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $zipPath) -or (Get-Item -LiteralPath $zipPath).Length -le 0) {
            throw "Failed to create non-empty ZIP archive '$zipPath'."
        }
        Write-Host "Created $zipPath" -ForegroundColor Green
    }
    Write-Host "Portable release ready: $OutputRoot" -ForegroundColor Green
} finally {
    if (-not $KeepStaging -and (Test-Path -LiteralPath $staging)) {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    } elseif ($KeepStaging) {
        Write-Host "Kept staging directory: $staging" -ForegroundColor DarkGray
    }
}
