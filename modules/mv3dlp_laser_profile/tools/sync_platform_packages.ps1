Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$moduleRoot = Split-Path -Parent $PSScriptRoot
$windowsRoot = Join-Path $moduleRoot "windows_x64"
$linuxRoot = Join-Path $moduleRoot "linux_x86_64"
$distRoot = Join-Path $moduleRoot "dist"

$sharedItems = @(
    "CMakeLists.txt",
    "include",
    "src",
    "examples"
)

function Reset-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Copy-SharedItems {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetRoot
    )

    foreach ($item in $sharedItems) {
        Copy-Item -LiteralPath (Join-Path $moduleRoot $item) -Destination $TargetRoot -Recurse -Force
    }
}

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        Ensure-Directory -Path $directory
    }

    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Copy-TreeIfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return $false
    }

    Ensure-Directory -Path $Destination
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
    return $true
}

function Get-StatusLabel {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Value
    )

    if ($Value) {
        return "present"
    }

    return "missing"
}

function Initialize-WindowsPackage {
    Copy-SharedItems -TargetRoot $windowsRoot

    $vendorLibDir = Join-Path $windowsRoot "vendor\lib"
    $binDir = Join-Path $windowsRoot "bin"
    $clProtocolDir = Join-Path $binDir "CLProtocol\Win64_x64"
    $vtkDir = Join-Path $binDir "VtkResource\Win64_x64"

    Ensure-Directory -Path $vendorLibDir
    Ensure-Directory -Path $binDir
    Ensure-Directory -Path $clProtocolDir
    Ensure-Directory -Path $vtkDir

    $vendorImportLib = "D:\3DMVS\Development\Libraries\win64\Mv3dLp.lib"
    $vendorImportLibCopied = $false
    if (Test-Path -LiteralPath $vendorImportLib) {
        Copy-Item -LiteralPath $vendorImportLib -Destination $vendorLibDir -Force
        $vendorImportLibCopied = $true
    }

    $sdkRuntimeCopied = Copy-TreeIfExists `
        -Source "C:\Program Files (x86)\Common Files\Mv3dLpSDK\Runtime\Win64_x64" `
        -Destination $binDir

    $mv3dRuntimeCopied = Copy-TreeIfExists `
        -Source "C:\Program Files (x86)\Common Files\MV3D\Runtime\Win64_x64" `
        -Destination $binDir

    [void](Copy-TreeIfExists `
        -Source "C:\Program Files (x86)\Common Files\MV3D\Runtime\CLProtocol\Win64_x64" `
        -Destination $clProtocolDir)

    [void](Copy-TreeIfExists `
        -Source "C:\Program Files (x86)\Common Files\Mv3dLpSDK\Runtime\VtkResource\Win64_x64" `
        -Destination $vtkDir)

    $readme = @'
# mv3dlp_laser_profile Windows x64

This is the Windows x64 delivery package.

## Contents

- `include/`: public C++ headers
- `src/`: wrapper implementation
- `examples/fetch_frame.cpp`: minimal usage example
- `bin/`: packaged vendor runtime DLLs, config files, and runtime dependencies
- `vendor/lib/Mv3dLp.lib`: vendor import library

## Usage

```cpp
#include "mv3dlp_laser_profile/driver.hpp"

mv3dlp::Driver driver;
auto devices = driver.enumerateDevices();
driver.connectBySerial(devices.front().serial_number);
driver.setAcquisitionMode(mv3dlp::AcquisitionMode::range_image);
driver.startAcquisition();
auto frame = driver.fetchFrame(std::chrono::milliseconds{1000});
driver.stopAcquisition();
driver.disconnect();
```

## Notes

- The wrapper loads `bin/Mv3dLp.dll` at runtime.
- If you move the DLLs elsewhere, set `DriverOptions.library_path`.

## Package status

- `Mv3dLp.lib`: __VENDOR_IMPORT_LIB__
- `Mv3dLpSDK runtime`: __SDK_RUNTIME__
- `MV3D runtime`: __MV3D_RUNTIME__
'@

    $readme = $readme.Replace("__VENDOR_IMPORT_LIB__", (Get-StatusLabel -Value $vendorImportLibCopied))
    $readme = $readme.Replace("__SDK_RUNTIME__", (Get-StatusLabel -Value $sdkRuntimeCopied))
    $readme = $readme.Replace("__MV3D_RUNTIME__", (Get-StatusLabel -Value $mv3dRuntimeCopied))

    Write-Utf8File -Path (Join-Path $windowsRoot "README.md") -Content $readme
}

function Initialize-LinuxPackage {
    Copy-SharedItems -TargetRoot $linuxRoot

    $vendorDir = Join-Path $linuxRoot "vendor\linux-x86_64"
    $libDir = Join-Path $linuxRoot "lib"
    $binDir = Join-Path $linuxRoot "bin"

    Ensure-Directory -Path $vendorDir
    Ensure-Directory -Path $libDir
    Ensure-Directory -Path $binDir

    $vendorNote = @'
Place the official vendor Linux runtime libraries here.

Expected minimum file:

libMv3dLp.so

If the vendor SDK ships additional dependent .so files, place them in the same folder.
'@
    Write-Utf8File -Path (Join-Path $vendorDir "README.md") -Content $vendorNote

    $linuxReadme = @'
# mv3dlp_laser_profile Linux x86_64

This is the Linux x86_64 delivery package.

## Contents

- `include/`: public C++ headers
- `src/`: wrapper implementation
- `examples/fetch_frame.cpp`: minimal usage example
- `vendor/linux-x86_64/`: drop the official vendor `.so` files here

## Usage

```cpp
#include "mv3dlp_laser_profile/driver.hpp"

mv3dlp::DriverOptions options;
options.library_path = "./vendor/linux-x86_64/libMv3dLp.so";

mv3dlp::Driver driver(options);
auto devices = driver.enumerateDevices();
driver.connectBySerial(devices.front().serial_number);
driver.setAcquisitionMode(mv3dlp::AcquisitionMode::range_image);
driver.startAcquisition();
auto frame = driver.fetchFrame(std::chrono::milliseconds{1000});
driver.stopAcquisition();
driver.disconnect();
```

## Current status

- Linux wrapper code is ready.
- No vendor Linux `.so` was present in the local SDK installation checked on 2026-08-13.
- Add the official Linux runtime under `vendor/linux-x86_64/` before deployment.

## Notes

- The C++ API is kept aligned with the Windows package.
- For ROS integration later, keep ROS2 code in a separate package that depends on this wrapper.
'@
    Write-Utf8File -Path (Join-Path $linuxRoot "README.md") -Content $linuxReadme

    $libNote = @'
No vendor Linux .so was available in the local SDK installation on 2026-08-13.

Drop the official runtime here or under vendor/linux-x86_64 and point DriverOptions.library_path to it.
'@
    Write-Utf8File -Path (Join-Path $libDir "README.md") -Content $libNote
}

function Create-Zip {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$ZipPath
    )

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    Compress-Archive -Path (Join-Path $SourcePath "*") -DestinationPath $ZipPath -CompressionLevel Optimal
}

Reset-Directory -Path $windowsRoot
Reset-Directory -Path $linuxRoot
Ensure-Directory -Path $distRoot

Initialize-WindowsPackage
Initialize-LinuxPackage

Create-Zip -SourcePath $windowsRoot -ZipPath (Join-Path $distRoot "mv3dlp_laser_profile_windows_x64.zip")
Create-Zip -SourcePath $linuxRoot -ZipPath (Join-Path $distRoot "mv3dlp_laser_profile_linux_x86_64.zip")

Write-Host "Platform packages refreshed."
