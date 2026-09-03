[CmdletBinding()]
param(
    [string]$SshHost = "192.168.1.20",
    [string]$SshUser = "pc",
    [int]$SshPort = 22,
    [string]$SshTargetRoot = "C:/Users/pc/Desktop/target_board_release/target_board_release",
    [string]$SshPassword = "",
    [string]$SshHostKey = "ssh-ed25519 255 SHA256:LR+aZBKOfwmRq3cAgo83azdq9UtqAK0GtlyDU9D5i9E",
    [string]$PscpPath = "",
    [string]$Package = "",
    [string]$RosRoot = "",
    [string]$WorkspaceRoot = "",
    [string]$BuildBase = "",
    [string]$InstallBase = "",
    [string]$LogBase = "",
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

# Build locally, but leave the release directory untouched until the SSH
# upload succeeds. The target already has its complete runtime from the first
# portable deployment; only the install overlay is sent on later iterations.
$buildArgs = @{ NoSync = $true }
foreach ($name in @("Package", "RosRoot", "WorkspaceRoot", "BuildBase", "InstallBase", "LogBase", "CmakeGenerator", "CxxCompiler", "CCompiler")) {
    $value = Get-Variable -Name $name -ValueOnly
    if (-not [string]::IsNullOrWhiteSpace([string]$value)) {
        $buildArgs[$name] = [string]$value
    }
}
& $incrementalScript @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Incremental build failed; SSH upload was not started." }

if ([string]::IsNullOrWhiteSpace($InstallBase)) {
    $InstallBase = Join-Path $repoRoot "tmp\incremental_install"
}
$InstallBase = [IO.Path]::GetFullPath($InstallBase)
if (-not (Test-Path -LiteralPath (Join-Path $InstallBase "lib"))) {
    throw "Incremental install output is missing: $InstallBase"
}

$pscp = if ([string]::IsNullOrWhiteSpace($PscpPath)) {
    Join-Path $PSScriptRoot "tools\pscp.exe"
} else {
    $PscpPath
}
if (-not (Test-Path -LiteralPath $pscp)) {
    throw "pscp.exe was not found: $pscp"
}
if ([string]::IsNullOrWhiteSpace($SshPassword)) {
    throw "SSH password is empty. Run ssh_incremental_update.cmd or pass -SshPassword explicitly."
}
$remoteRoot = ($SshTargetRoot -replace '\\', '/').TrimEnd('/')
$remoteInstall = $remoteRoot + "/install"
$remoteInstallSpec = "{0}@{1}:{2}" -f $SshUser, $SshHost, $remoteInstall
$remoteRootSpec = "{0}@{1}:{2}" -f $SshUser, $SshHost, $remoteRoot
$entries = Get-ChildItem -LiteralPath $InstallBase -Force
if (-not $entries) { throw "Incremental install output is empty: $InstallBase" }

function Invoke-PscpUpload([string[]]$localPaths, [string]$remoteSpec, [string]$description) {
    if (-not $localPaths -or $localPaths.Count -eq 0) {
        throw "No local paths were provided for $description."
    }
    Write-Host "Uploading $description to $remoteSpec" -ForegroundColor Cyan
    $pscpArgs = @(
        "-batch",
        "-hostkey", $SshHostKey,
        "-pw", $SshPassword,
        "-P", "$SshPort",
        "-r"
    )
    $pscpArgs += $localPaths
    $pscpArgs += $remoteSpec
    & $pscp @pscpArgs
    if ($LASTEXITCODE -ne 0) {
        throw "SSH upload failed while transferring $description (pscp exit $LASTEXITCODE). Verify the target folder '$SshTargetRoot' exists and is writable."
    }
}

Write-Host "Using the configured SSH password; no password prompt will appear." -ForegroundColor DarkGray
# PSCP is bundled with this development helper because native OpenSSH scp
# intentionally has no non-interactive password option. The password is
# visible in this local script by explicit user request; never reuse it for
# an account with access beyond this robot board.

Invoke-PscpUpload (@($entries | ForEach-Object { $_.FullName })) $remoteInstallSpec "install overlay"

$releaseLauncherFiles = @(
    (Join-Path $repoRoot "target_board_release\run_robot.ps1")
    (Join-Path $repoRoot "target_board_release\stop_robot.ps1")
    (Join-Path $repoRoot "target_board_release\start_monitor.cmd")
)
$releaseConfigFile = Join-Path $repoRoot "target_board_release\config\robot.yaml"
$fastDdsProfileFile = Join-Path $repoRoot "target_board_release\config\fastdds_profile.xml"
foreach ($path in @($releaseLauncherFiles) + @($releaseConfigFile)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required release launcher file is missing: $path"
    }
}
Invoke-PscpUpload $releaseLauncherFiles $remoteRootSpec "release launcher files"
Invoke-PscpUpload @($releaseConfigFile) ($remoteRootSpec + "/config/robot.yaml") "release configuration file"
Invoke-PscpUpload @($fastDdsProfileFile) ($remoteRootSpec + "/config/fastdds_profile.xml") "Fast DDS network profile"

Write-Host "SSH incremental update complete. Runtime and ZIP were not transferred." -ForegroundColor Green
