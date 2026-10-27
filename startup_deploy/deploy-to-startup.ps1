# ============================================================================
# PA1664 Automatic Deployment and Packaging Script - OPTIMIZED
# Package compiled programs with all dependencies to startup_deploy directory
# Quick mode: update EXE only, skip dependency collection
# ============================================================================

param(
    [switch]$Force,
    [switch]$QuickMode
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# ============================================================================
# Path Configuration
# ============================================================================

$binDir = Join-Path $projectRoot 'bin'
$runtimeDir = Join-Path $projectRoot 'runtime'
$startupDeployDir = Join-Path $projectRoot 'startup_deploy'

$hostAppDir = Join-Path $startupDeployDir 'robot_host\app'
$workbenchAppDir = Join-Path $startupDeployDir 'operator_workbench\app'

$hostExe = 'PA1664RobotHost.exe'
$workbenchExe = 'PA1664Workbench.exe'

# ============================================================================
# Utility Functions
# ============================================================================

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host ("=" * 80) -ForegroundColor Cyan
    Write-Host " $Message" -ForegroundColor Cyan
    Write-Host ("=" * 80) -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Text)
    Write-Host "  [OK] $Text" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Text)
    Write-Host "  [FAIL] $Text" -ForegroundColor Red
}

function Get-Sha256 {
    param([string]$path)
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

function Copy-IfDifferent {
    param(
        [string]$sourceFile,
        [string]$destinationFile,
        [switch]$Quiet
    )
    
    $needCopy = $true
    if (Test-Path -LiteralPath $destinationFile) {
        $srcInfo = Get-Item -LiteralPath $sourceFile
        $dstInfo = Get-Item -LiteralPath $destinationFile
        if ($srcInfo.Length -eq $dstInfo.Length) {
            $srcHash = Get-Sha256 $sourceFile
            $dstHash = Get-Sha256 $destinationFile
            if ($srcHash -eq $dstHash) {
                $needCopy = $false
            }
        }
    }
    
    if ($needCopy) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationFile) -Force | Out-Null
        Copy-Item -LiteralPath $sourceFile -Destination $destinationFile -Force
        if (-not $Quiet) {
            Write-Host "  [COPY] $(Split-Path -Leaf $sourceFile)" -ForegroundColor Green
        }
    }
}

# ============================================================================
# STEP 1: Check build outputs
# ============================================================================

Write-Step "STEP 1: Checking build outputs"

$hostExePath = Join-Path $binDir $hostExe
$workbenchExePath = Join-Path $binDir $workbenchExe

$hostExists = Test-Path -LiteralPath $hostExePath
$workbenchExists = Test-Path -LiteralPath $workbenchExePath

if (-not $hostExists) {
    Write-Fail "Robot host program not found: $hostExe"
    Write-Host "  Path: $hostExePath" -ForegroundColor Yellow
}

if (-not $workbenchExists) {
    Write-Fail "Operator workbench program not found: $workbenchExe"
    Write-Host "  Path: $workbenchExePath" -ForegroundColor Yellow
}

if (-not $hostExists -or -not $workbenchExists) {
    Write-Host ""
    Write-Host "Please compile the programs first:" -ForegroundColor Yellow
    Write-Host "  - PA1664RobotHost.pro" -ForegroundColor White
    Write-Host "  - PA1664Workbench.pro" -ForegroundColor White
    exit 1
}

Write-Success "PA1664RobotHost.exe found"
Write-Success "PA1664Workbench.exe found"

# ============================================================================
# STEP 2: Prepare deployment directories
# ============================================================================

Write-Step "STEP 2: Preparing deployment directories"

if ($QuickMode) {
    Write-Host "  MODE: QUICK - EXE update only" -ForegroundColor Magenta
    
    # Check if app directories exist
    if (-not (Test-Path $hostAppDir)) {
        Write-Fail "robot_host\app not found - run full build first"
        exit 1
    }
    if (-not (Test-Path $workbenchAppDir)) {
        Write-Fail "operator_workbench\app not found - run full build first"
        exit 1
    }
    
    # Quick mode: only update EXE files
    Write-Host "  Updating EXE files only..." -ForegroundColor Yellow
    
    Copy-Item -LiteralPath $hostExePath -Destination (Join-Path $hostAppDir $hostExe) -Force
    $hostSize = [Math]::Round((Get-Item $hostExePath).Length / 1MB, 2)
    Write-Success "PA1664RobotHost.exe updated - $hostSize MB"
    
    Copy-Item -LiteralPath $workbenchExePath -Destination (Join-Path $workbenchAppDir $workbenchExe) -Force
    $workbenchSize = [Math]::Round((Get-Item $workbenchExePath).Length / 1MB, 2)
    Write-Success "PA1664Workbench.exe updated - $workbenchSize MB"
    
    Write-Host ""
    Write-Step "Quick Deployment Complete"
    Write-Host ""
    Write-Host "EXE files updated:" -ForegroundColor Cyan
    Write-Host "  startup_deploy\robot_host\app\PA1664RobotHost.exe" -ForegroundColor White
    Write-Host "  startup_deploy\operator_workbench\app\PA1664Workbench.exe" -ForegroundColor White
    Write-Host ""
    Write-Host "Dependencies (DLL, plugins) were NOT updated." -ForegroundColor Yellow
    Write-Host "If dependencies changed, run full build without -QuickMode" -ForegroundColor Yellow
    Write-Host ""
    exit 0
}

# ============================================================================
# FULL MODE: Complete deployment with all dependencies
# ============================================================================

if ($Force) {
    Write-Host "  Force mode: cleaning old app directories..." -ForegroundColor Yellow
    if (Test-Path $hostAppDir) {
        Remove-Item -Path $hostAppDir -Recurse -Force
    }
    if (Test-Path $workbenchAppDir) {
        Remove-Item -Path $workbenchAppDir -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $hostAppDir -Force | Out-Null
New-Item -ItemType Directory -Path $workbenchAppDir -Force | Out-Null

# Remove placeholder files
$placeholderFiles = @(
    (Join-Path $hostAppDir 'PLACEHOLDER_COMPILED_BINARIES_GO_HERE.txt'),
    (Join-Path $workbenchAppDir 'PLACEHOLDER_COMPILED_BINARIES_GO_HERE.txt')
)

foreach ($file in $placeholderFiles) {
    if (Test-Path $file) {
        Remove-Item $file -Force
    }
}

Write-Success "Deployment directories ready"

# ============================================================================
# STEP 3: Copy main EXE files
# ============================================================================

Write-Step "STEP 3: Copying main executables"

Copy-IfDifferent $hostExePath (Join-Path $hostAppDir $hostExe)
Copy-IfDifferent $workbenchExePath (Join-Path $workbenchAppDir $workbenchExe)

Write-Success "Main executables copied"

# ============================================================================
# STEP 4: Copy DLL dependencies from bin directory
# ============================================================================

Write-Step "STEP 4: Collecting DLL dependencies from bin directory"

if (-not (Test-Path $binDir)) {
    Write-Fail "bin directory not found: $binDir"
    exit 1
}

$dllCount = 0
$runtimeFiles = @()

Get-ChildItem -Path $binDir -Filter *.dll -File | ForEach-Object {
    Copy-IfDifferent $_.FullName (Join-Path $hostAppDir $_.Name) -Quiet
    Copy-IfDifferent $_.FullName (Join-Path $workbenchAppDir $_.Name) -Quiet
    $runtimeFiles += $_.Name
    $dllCount++
}

Write-Success "Copied $dllCount DLL files"

# ============================================================================
# STEP 5: Copy Qt plugins
# ============================================================================

Write-Step "STEP 5: Copying Qt plugins"

$pluginsDir = Join-Path $binDir 'plugins'
if (Test-Path $pluginsDir) {
    $hostPluginsDir = Join-Path $hostAppDir 'plugins'
    $workbenchPluginsDir = Join-Path $workbenchAppDir 'plugins'
    
    Get-ChildItem -Path $pluginsDir -Directory | ForEach-Object {
        $pluginCategory = $_.Name
        $hostTargetDir = Join-Path $hostPluginsDir $pluginCategory
        $workbenchTargetDir = Join-Path $workbenchPluginsDir $pluginCategory
        
        New-Item -ItemType Directory -Path $hostTargetDir -Force | Out-Null
        New-Item -ItemType Directory -Path $workbenchTargetDir -Force | Out-Null
        
        Get-ChildItem -Path $_.FullName -File -Recurse | ForEach-Object {
            $relativePath = $_.FullName.Substring($pluginsDir.Length).TrimStart('\')
            Copy-IfDifferent $_.FullName (Join-Path $hostPluginsDir $relativePath) -Quiet
            Copy-IfDifferent $_.FullName (Join-Path $workbenchPluginsDir $relativePath) -Quiet
        }
        
        Write-Host "    Plugin category: $pluginCategory" -ForegroundColor Gray
    }
    
    Write-Success "Qt plugins copied"
} else {
    Write-Host "  No plugins directory found, skipping" -ForegroundColor Yellow
}

# ============================================================================
# STEP 6: Copy configuration directories
# ============================================================================

Write-Step "STEP 6: Copying configuration directories"

$configDirs = @('config', 'configs')

foreach ($dir in $configDirs) {
    $sourceDir = Join-Path $binDir $dir
    if (Test-Path $sourceDir) {
        $hostTargetDir = Join-Path $hostAppDir $dir
        $workbenchTargetDir = Join-Path $workbenchAppDir $dir
        
        New-Item -ItemType Directory -Path $hostTargetDir -Force | Out-Null
        New-Item -ItemType Directory -Path $workbenchTargetDir -Force | Out-Null
        
        Get-ChildItem -Path $sourceDir -File -Recurse | ForEach-Object {
            $relativePath = $_.FullName.Substring($sourceDir.Length).TrimStart('\')
            Copy-IfDifferent $_.FullName (Join-Path $hostTargetDir $relativePath) -Quiet
            Copy-IfDifferent $_.FullName (Join-Path $workbenchTargetDir $relativePath) -Quiet
        }
        
        Write-Success "$dir directory copied"
    }
}

# ============================================================================
# STEP 7: Copy additional dependencies from runtime directory
# ============================================================================

Write-Step "STEP 7: Copying additional dependencies from runtime directory"

if (Test-Path -LiteralPath $runtimeDir) {
    $runtimeFileCount = 0
    Get-ChildItem -LiteralPath $runtimeDir -Recurse -File -Force | ForEach-Object {
        $relativePath = $_.FullName.Substring($runtimeDir.Length).TrimStart('\')
        
        # Skip files already copied from bin
        if ($runtimeFiles -notcontains $_.Name) {
            Copy-IfDifferent $_.FullName (Join-Path $hostAppDir $relativePath) -Quiet
            Copy-IfDifferent $_.FullName (Join-Path $workbenchAppDir $relativePath) -Quiet
            $runtimeFileCount++
        }
    }
    Write-Success "Copied $runtimeFileCount additional files from runtime"
} else {
    Write-Host "  Runtime directory not found, skipping" -ForegroundColor Yellow
}

# ============================================================================
# STEP 8: Create necessary subdirectories
# ============================================================================

Write-Step "STEP 8: Creating necessary subdirectories"

$requiredDirs = @('logs', 'recordings', 'screenshots')

foreach ($dir in $requiredDirs) {
    New-Item -ItemType Directory -Path (Join-Path $hostAppDir $dir) -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $workbenchAppDir $dir) -Force | Out-Null
}

Write-Success "Created logs, recordings, screenshots directories"

# ============================================================================
# STEP 9: Verify deployment results
# ============================================================================

Write-Step "STEP 9: Verifying deployment"

function Test-Deployment {
    param(
        [string]$appDir,
        [string]$exeName,
        [string]$programName
    )
    
    $exePath = Join-Path $appDir $exeName
    $valid = $true
    
    Write-Host ""
    Write-Host "  Checking $programName..." -ForegroundColor Cyan
    
    if (-not (Test-Path $exePath)) {
        Write-Fail "Main executable missing: $exeName"
        $valid = $false
    } else {
        $exeSize = [Math]::Round((Get-Item $exePath).Length / 1MB, 2)
        Write-Success "$exeName - $exeSize MB"
    }
    
    # Check critical DLLs
    $criticalDlls = @('Qt6Core.dll', 'Qt6Network.dll', 'opencv_world500.dll', 'client.dll')
    $missingDlls = 0
    foreach ($dll in $criticalDlls) {
        $dllPath = Join-Path $appDir $dll
        if (-not (Test-Path $dllPath)) {
            Write-Host "    [WARN] Missing: $dll" -ForegroundColor Yellow
            $missingDlls++
        }
    }
    
    if ($missingDlls -eq 0) {
        Write-Success "All critical DLLs present"
    }
    
    # Count files and total size
    $files = Get-ChildItem -Path $appDir -Recurse -File
    $totalSize = [Math]::Round(($files | Measure-Object -Property Length -Sum).Sum / 1MB, 2)
    $fileCount = $files.Count
    
    Write-Host "    Total files: $fileCount" -ForegroundColor Gray
    Write-Host "    Total size: $totalSize MB" -ForegroundColor Gray
    
    return $valid
}

$hostValid = Test-Deployment $hostAppDir $hostExe "Robot Host"
$workbenchValid = Test-Deployment $workbenchAppDir $workbenchExe "Operator Workbench"

# ============================================================================
# Complete
# ============================================================================

Write-Host ""
Write-Step "Deployment Complete"

if ($hostValid -and $workbenchValid) {
    Write-Success "All programs packaged successfully to startup_deploy directory"
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Copy startup_deploy\robot_host folder to robot PC" -ForegroundColor White
    Write-Host "  2. Copy startup_deploy\operator_workbench folder to operator PC" -ForegroundColor White
    Write-Host "  3. Configure IP in robot_host\config\host.psd1" -ForegroundColor White
    Write-Host "  4. Configure robot IP in operator_workbench\config\workbench.psd1" -ForegroundColor White
    Write-Host "  5. Double-click startup CMD files to test" -ForegroundColor White
    Write-Host "  6. Install auto-start if testing succeeds" -ForegroundColor White
    Write-Host ""
    Write-Host "For detailed instructions, see: startup_deploy\README.md" -ForegroundColor Yellow
    Write-Host ""
    exit 0
} else {
    Write-Fail "Deployment completed with issues, please check errors above"
    exit 1
}

