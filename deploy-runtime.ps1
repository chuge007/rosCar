param(
    [string]$RemoteDesktopSource = $env:LANREMOTEQT_DIST_DIR
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $projectRoot 'runtime'
$destination = Join-Path $projectRoot 'bin'

New-Item -ItemType Directory -Path $destination -Force | Out-Null

function Get-Sha256([string]$path) {
    $stream = [System.IO.File]::OpenRead($path)
    try {
        $sha256 = New-Object System.Security.Cryptography.SHA256Managed
        try {
            return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '')
        }
        finally {
            $sha256.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Copy-IfDifferent([string]$sourceFile, [string]$destinationFile) {
    if (Test-Path -LiteralPath $destinationFile) {
        $srcInfo = Get-Item -LiteralPath $sourceFile
        $dstInfo = Get-Item -LiteralPath $destinationFile
        if ($srcInfo.Length -eq $dstInfo.Length) {
            $srcHash = Get-Sha256 $sourceFile
            $dstHash = Get-Sha256 $destinationFile
            if ($srcHash -eq $dstHash) { return }
        }
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $destinationFile -Force
}

Get-ChildItem -LiteralPath $source -Recurse -File -Force | ForEach-Object {
    $relative = $_.FullName.Substring($source.Length).TrimStart('\')
    $destinationFile = Join-Path $destination $relative
    # default_config.json and configs/ are live SDK state. Rebuilding must not
    # restore deleted workgroups or overwrite parameters saved by the user.
    # They are copied only for a fresh deployment.
    $isLiveSdkState = $relative -ieq 'default_config.json' -or
                      $relative -ilike 'configs\*' -or
                      $relative -ieq 'config\drive_settings.ini'
    if (-not ($isLiveSdkState -and (Test-Path -LiteralPath $destinationFile))) {
        Copy-IfDifferent $_.FullName $destinationFile
    }
}

$qtBin = 'D:\QT\6.8.3\msvc2022_64\bin'
$neededQt = @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
              'Qt6SerialPort.dll', 'Qt6Multimedia.dll',
              'avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll',
              'swresample-5.dll', 'swscale-8.dll')
foreach ($dll in $neededQt) {
    $candidate = Join-Path $qtBin $dll
    if (Test-Path -LiteralPath $candidate) {
        Copy-IfDifferent $candidate (Join-Path $destination $dll)
    }
}

$multimediaPlugins = Join-Path $qtBin '..\plugins\multimedia'
if (Test-Path -LiteralPath $multimediaPlugins) {
    Get-ChildItem -LiteralPath $multimediaPlugins -File -Filter '*.dll' |
        Where-Object { $_.Name -notlike '*plugind.dll' } | ForEach-Object {
        Copy-IfDifferent $_.FullName (Join-Path $destination ('multimedia\' + $_.Name))
    }
}

# LanRemoteQt remains a Qt 5 sidecar because its FFmpeg/libyuv build and Qt
# runtime have a different ABI from this Qt 6 application.  Keeping it in a
# child directory prevents Qt 5 DLLs from shadowing the workbench's Qt 6 DLLs.
if ([string]::IsNullOrWhiteSpace($RemoteDesktopSource)) {
    $developmentRemote = 'D:\Lz\eihtik\SDK\API\PCIE_API_Qt5_14_2_SVN9777\qtPCIEDemo\LanRemoteQt\dist-v1.2.2-pa1664-workbench'
    if (Test-Path -LiteralPath $developmentRemote) {
        $RemoteDesktopSource = $developmentRemote
    }
}
if (-not [string]::IsNullOrWhiteSpace($RemoteDesktopSource)) {
    $resolvedRemote = (Resolve-Path -LiteralPath $RemoteDesktopSource).Path
    $remoteDestination = Join-Path $destination 'RemoteDesktop'
    Get-ChildItem -LiteralPath $resolvedRemote -Recurse -File -Force | ForEach-Object {
        $relative = $_.FullName.Substring($resolvedRemote.Length).TrimStart('\')
        Copy-IfDifferent $_.FullName (Join-Path $remoteDestination $relative)
    }
    Write-Host "Remote desktop deployed to $remoteDestination"
}

Write-Host "Runtime deployed to $destination"
