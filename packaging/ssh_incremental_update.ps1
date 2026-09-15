[CmdletBinding()]
param(
    [string]$SshHost = "192.168.1.20",
    [string]$SshUser = "pc",
    [int]$SshPort = 22,
    [string]$SshTargetRoot = "C:/Users/pc/Desktop/target_board_release/target_board_release",
    [string]$SshPassword = "",
    [string]$SshKeyPath = "",
    [string]$SshHostKey = "ssh-ed25519 255 SHA256:LR+aZBKOfwmRq3cAgo83azdq9UtqAK0GtlyDU9D5i9E",
    [string]$PscpPath = "",
    [string]$Package = "",
    [string]$RosRoot = "",
    [string]$WorkspaceRoot = "",
    [string]$BuildBase = "",
    [string]$InstallBase = "",
    [string]$LogBase = "",
    [int]$ParallelWorkers = 0,
    [switch]$BuildTests,
    [string]$CmakeGenerator = "Ninja",
    [string]$CxxCompiler = "",
    [string]$CCompiler = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$incrementalScript = Join-Path $PSScriptRoot "incremental_update.ps1"
if (-not (Test-Path -LiteralPath $incrementalScript)) {
    throw "Missing incremental build script: $incrementalScript"
}

# Build directly into the local release install overlay. The target already
# has its complete runtime from the first portable deployment; only the
# install overlay is sent on later iterations.
$buildArgs = @{ NoSync = $true; BuildTests = $BuildTests }
foreach ($name in @("Package", "RosRoot", "WorkspaceRoot", "BuildBase", "InstallBase", "LogBase", "ParallelWorkers", "CmakeGenerator", "CxxCompiler", "CCompiler")) {
    $value = Get-Variable -Name $name -ValueOnly
    if (-not [string]::IsNullOrWhiteSpace([string]$value)) {
        $buildArgs[$name] = [string]$value
    }
}
& $incrementalScript @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Incremental build failed; SSH upload was not started." }

if ([string]::IsNullOrWhiteSpace($InstallBase)) {
    $InstallBase = Join-Path $repoRoot "target_board_release\install"
}
$InstallBase = [IO.Path]::GetFullPath($InstallBase)
if (-not (Test-Path -LiteralPath (Join-Path $InstallBase "lib"))) {
    throw "Incremental install output is missing: $InstallBase"
}

$scp = (Get-Command scp.exe -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($scp)) { throw "OpenSSH scp.exe was not found in PATH." }
if ([string]::IsNullOrWhiteSpace($SshKeyPath)) {
    $SshKeyPath = if ($SshHost -eq "192.168.1.21") {
        Join-Path $HOME ".ssh\id_ed25519_robot21"
    } else {
        Join-Path $HOME ".ssh\id_ed25519"
    }
}
$SshKeyPath = [IO.Path]::GetFullPath($SshKeyPath)
if (-not (Test-Path -LiteralPath $SshKeyPath)) { throw "SSH private key was not found: $SshKeyPath" }
$remoteRoot = ($SshTargetRoot -replace '\\', '/').TrimEnd('/')
$remoteInstall = $remoteRoot + "/install"
$remoteInstallSpec = "{0}@{1}:{2}" -f $SshUser, $SshHost, $remoteInstall
$remoteRootSpec = "{0}@{1}:{2}" -f $SshUser, $SshHost, $remoteRoot
$entries = Get-ChildItem -LiteralPath $InstallBase -Force
if (-not $entries) { throw "Incremental install output is empty: $InstallBase" }

function Invoke-ScpUpload([string[]]$localPaths, [string]$remoteSpec, [string]$description) {
    if (-not $localPaths -or $localPaths.Count -eq 0) {
        throw "No local paths were provided for $description."
    }
    Write-Host "Uploading $description to $remoteSpec using $SshKeyPath" -ForegroundColor Cyan
    $scpArgs = @(
        "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=yes",
        "-i", $SshKeyPath,
        "-P", "$SshPort",
        "-r"
    )
    $scpArgs += $localPaths
    $scpArgs += $remoteSpec
    & $scp @scpArgs
    if ($LASTEXITCODE -ne 0) {
        throw "SSH upload failed while transferring $description (scp exit $LASTEXITCODE). Verify the public key is installed for '$SshUser' and the target folder '$SshTargetRoot' exists."
    }
}

Write-Host "Using SSH key authentication; no password prompt will appear." -ForegroundColor DarkGray

Invoke-ScpUpload (@($entries | ForEach-Object { $_.FullName })) $remoteInstallSpec "install overlay"

$releaseLauncherFiles = @(
    (Join-Path $repoRoot "target_board_release\run_robot.ps1")
    (Join-Path $repoRoot "target_board_release\run_drive_test.ps1")
    (Join-Path $repoRoot "target_board_release\run_drive_test.cmd")
    (Join-Path $repoRoot "target_board_release\stop_robot.ps1")
    (Join-Path $repoRoot "target_board_release\start_monitor.cmd")
)
$releaseConfigFile = Join-Path $repoRoot "target_board_release\config\robot.yaml"
$driveTestConfigFile = Join-Path $repoRoot "target_board_release\config\drive_test.yaml"
$fastDdsProfileFile = Join-Path $repoRoot "target_board_release\config\fastdds_profile.xml"
foreach ($path in @($releaseLauncherFiles) + @($releaseConfigFile, $driveTestConfigFile, $fastDdsProfileFile)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required release launcher file is missing: $path"
    }
}
Invoke-ScpUpload $releaseLauncherFiles $remoteRootSpec "release launcher files"
Invoke-ScpUpload @($releaseConfigFile) ($remoteRootSpec + "/config/robot.yaml") "release configuration file"
Invoke-ScpUpload @($driveTestConfigFile) ($remoteRootSpec + "/config/drive_test.yaml") "drive-test configuration file"
Invoke-ScpUpload @($fastDdsProfileFile) ($remoteRootSpec + "/config/fastdds_profile.xml") "Fast DDS network profile"

Write-Host "SSH incremental update complete. Runtime and ZIP were not transferred." -ForegroundColor Green
