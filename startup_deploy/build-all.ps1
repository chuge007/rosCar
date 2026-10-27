# ============================================================================
# PA1664 Build and Deploy Script - OPTIMIZED
# Auto compile Host + Workbench, then package to startup_deploy
# Quick mode: skip dependency collection for faster iterative builds
# ============================================================================

param(
    [switch]$SkipBuild,
    [switch]$Force,
    [switch]$QuickMode,
    [string]$QtPath = ""
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Write-Title {
    param([string]$Text)
    Write-Host ""
    Write-Host ("=" * 80) -ForegroundColor Cyan
    Write-Host " $Text" -ForegroundColor Cyan
    Write-Host ("=" * 80) -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step {
    param([string]$Text)
    Write-Host ""
    Write-Host ">>> $Text" -ForegroundColor Yellow
}

function Write-Success {
    param([string]$Text)
    Write-Host "  [OK] $Text" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Text)
    Write-Host "  [FAIL] $Text" -ForegroundColor Red
}

function Write-Warn {
    param([string]$Text)
    Write-Host "  [WARN] $Text" -ForegroundColor Yellow
}

# ============================================================================
# Find Qt build tools
# ============================================================================

Write-Title "PA1664 Auto Build and Deploy"

if ($QuickMode) {
    Write-Host "  MODE: QUICK (skip dependency collection)" -ForegroundColor Magenta
}

if (-not $SkipBuild) {
    Write-Step "Finding Qt build tools..."
    
    # Find qmake
    $qmake = $null
    $qtDir = $null
    
    if ($QtPath -and (Test-Path $QtPath)) {
        $qmake = Join-Path $QtPath 'bin\qmake.exe'
        if (Test-Path $qmake) {
            $qtDir = $QtPath
            Write-Success "Using specified Qt: $QtPath"
        }
    }
    
    if (-not $qmake) {
        $qmakePaths = @(
            'D:\qt5.15.2\6.8.3\msvc2022_64',
            'D:\QT\6.8.3\msvc2022_64',
            'C:\Qt\6.8.3\msvc2022_64',
            'D:\qt5.15.2\6.8.0\msvc2022_64',
            'D:\QT\6.8.0\msvc2022_64',
            'C:\Qt\6.8.0\msvc2022_64'
        )
        
        foreach ($path in $qmakePaths) {
            $testQmake = Join-Path $path 'bin\qmake.exe'
            if (Test-Path $testQmake) {
                $qmake = $testQmake
                $qtDir = $path
                Write-Success "Found Qt: $qtDir"
                break
            }
        }
    }
    
    if (-not $qmake) {
        $qmakeInPath = Get-Command qmake -ErrorAction SilentlyContinue
        if ($qmakeInPath) {
            $qmake = $qmakeInPath.Source
            $qtDir = Split-Path (Split-Path $qmake -Parent) -Parent
            Write-Success "Found Qt in PATH: $qtDir"
        }
    }
    
    if (-not $qmake) {
        Write-Fail "qmake not found"
        Write-Host ""
        Write-Host "Solutions:" -ForegroundColor Yellow
        Write-Host "  1. Install Qt 6.8.x MSVC 2022 64-bit" -ForegroundColor White
        Write-Host "  2. Or specify Qt path: .\build-all.ps1 -QtPath 'D:\Qt\6.8.3\msvc2022_64'" -ForegroundColor White
        Write-Host "  3. Or compile manually then run: .\build-all.ps1 -SkipBuild" -ForegroundColor White
        exit 1
    }
    
    # Find jom or nmake
    $builder = $null
    $builderName = ""
    
    $jomPaths = @(
        'D:\qt5.15.2\Tools\QtCreator\bin\jom\jom.exe',
        'C:\Qt\Tools\QtCreator\bin\jom\jom.exe',
        (Join-Path (Split-Path $qtDir -Parent) 'Tools\QtCreator\bin\jom\jom.exe')
    )
    
    foreach ($path in $jomPaths) {
        if (Test-Path $path) {
            $builder = $path
            $builderName = "jom"
            Write-Success "Found jom: $builder"
            break
        }
    }
    
    if (-not $builder) {
        $nmakeInPath = Get-Command nmake -ErrorAction SilentlyContinue
        if ($nmakeInPath) {
            $builder = 'nmake'
            $builderName = "nmake"
            Write-Success "Found nmake"
        }
    }
    
    if (-not $builder) {
        Write-Fail "jom or nmake not found"
        Write-Host ""
        Write-Host "Solutions:" -ForegroundColor Yellow
        Write-Host "  1. Open 'x64 Native Tools Command Prompt for VS 2022'" -ForegroundColor White
        Write-Host "  2. cd to project directory" -ForegroundColor White
        Write-Host "  3. Run: powershell -ExecutionPolicy Bypass -File .\build-all.ps1" -ForegroundColor White
        Write-Host ""
        Write-Host "  Or install Qt Creator (includes jom)" -ForegroundColor White
        exit 1
    }
    
    # Load MSVC environment (cl.exe, link.exe, INCLUDE, LIB) if not already loaded
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $vsPath = $null
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        }
        $vcvars = if ($vsPath) { Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat' } else { $null }
        if (-not $vcvars -or -not (Test-Path $vcvars)) {
            Write-Fail "Visual Studio 2022 C++ tools (vcvars64.bat) not found"
            exit 1
        }
        cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
            if ($_ -match '^([^=]+)=(.*)$') {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
            }
        }
        if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
            Write-Fail "Failed to load MSVC environment from $vcvars"
            exit 1
        }
        Write-Success "MSVC environment loaded: $vcvars"
    } else {
        Write-Success "MSVC environment already loaded"
    }
    # Set Qt environment
    $env:PATH = "$qtDir\bin;$env:PATH"
    Write-Success "Qt environment set"
}

# ============================================================================
# Build function
# ============================================================================

function Build-QtProject {
    param(
        [string]$ProjectName,
        [string]$ProFile,
        [string]$QmakePath,
        [string]$Builder,
        [string]$BuilderName
    )
    
    Write-Step "Building $ProjectName..."
    
    $buildDir = Join-Path $projectRoot "build-$ProjectName-temp"
    
    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    
    try {
        Push-Location $buildDir
        
        Write-Step "Generating build files with qmake..."
        & $QmakePath -spec win32-msvc CONFIG+=release $ProFile | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "qmake failed"
        }
        
        Write-Step "Compiling with $BuilderName..."
        if ($BuilderName -eq "jom") {
            & $Builder -j4 | Out-Host
        } else {
            & nmake | Out-Host
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Compilation failed"
        }
        
        # Check if executable was built directly to bin directory (DESTDIR set in .pro)
        $binDir = Join-Path $projectRoot 'bin'
        $exeName = "$ProjectName.exe"
        $targetExe = Join-Path $binDir $exeName
        
        if (Test-Path $targetExe) {
            # Already in bin directory, no need to copy
            Write-Host "  Executable found in bin directory (DESTDIR used)" -ForegroundColor Gray
        } else {
            # Check traditional release subdirectory
            $releaseBin = Join-Path (Get-Location) 'release'
            if (-not (Test-Path $releaseBin)) {
                throw "No release directory or bin output found after build"
            }
            
            $exePath = Join-Path $releaseBin $exeName
            
            if (-not (Test-Path $exePath)) {
                throw "Executable not found: $exeName"
            }
            
            if (-not (Test-Path $binDir)) {
                New-Item -ItemType Directory -Path $binDir -Force | Out-Null
            }
            
            Copy-Item $exePath $targetExe -Force
        }
        
        $sizeMB = [Math]::Round((Get-Item $targetExe).Length / 1MB, 2)
        Write-Success "$ProjectName.exe compiled - $sizeMB MB"
        
        return $true
    }
    catch {
        Write-Fail "$ProjectName build failed: $_"
        return $false
    }
    finally {
        Pop-Location
        
        if (Test-Path $buildDir) {
            Remove-Item $buildDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# ============================================================================
# Build two projects
# ============================================================================

$binDir = Join-Path $projectRoot 'bin'
$hostExe = Join-Path $binDir 'PA1664RobotHost.exe'
$workbenchExe = Join-Path $binDir 'PA1664Workbench.exe'

$compileFailed = $false

if (-not $SkipBuild) {
    # Build PA1664RobotHost
    $hostPro = Join-Path $projectRoot 'PA1664RobotHost.pro'
    if (Test-Path $hostPro) {
        $hostSuccess = Build-QtProject -ProjectName "PA1664RobotHost" `
                                       -ProFile $hostPro `
                                       -QmakePath $qmake `
                                       -Builder $builder `
                                       -BuilderName $builderName
        if (-not $hostSuccess) {
            $compileFailed = $true
        }
    } else {
        Write-Warn "PA1664RobotHost.pro not found"
        $compileFailed = $true
    }
    
    # Build PA1664Workbench
    $workbenchPro = Join-Path $projectRoot 'PA1664Workbench.pro'
    if (Test-Path $workbenchPro) {
        $workbenchSuccess = Build-QtProject -ProjectName "PA1664Workbench" `
                                            -ProFile $workbenchPro `
                                            -QmakePath $qmake `
                                            -Builder $builder `
                                            -BuilderName $builderName
        if (-not $workbenchSuccess) {
            $compileFailed = $true
        }
    } else {
        Write-Warn "PA1664Workbench.pro not found"
        $compileFailed = $true
    }
    
    if ($compileFailed) {
        Write-Host ""
        Write-Fail "Build errors occurred"
        Write-Host ""
        Write-Host "Suggestions:" -ForegroundColor Yellow
        Write-Host "  1. Compile manually in Qt Creator" -ForegroundColor White
        Write-Host "  2. Or run in VS 2022 Command Prompt" -ForegroundColor White
        Write-Host "  3. Then run: .\build-all.ps1 -SkipBuild" -ForegroundColor White
        exit 1
    }
}

# ============================================================================
# Verify build outputs
# ============================================================================

Write-Step "Verifying build outputs..."

$hostExists = Test-Path $hostExe
$workbenchExists = Test-Path $workbenchExe

if ($hostExists) {
    $sizeMB = [Math]::Round((Get-Item $hostExe).Length / 1MB, 2)
    Write-Success "PA1664RobotHost.exe ready - $sizeMB MB"
} else {
    Write-Fail "PA1664RobotHost.exe not found"
}

if ($workbenchExists) {
    $sizeMB = [Math]::Round((Get-Item $workbenchExe).Length / 1MB, 2)
    Write-Success "PA1664Workbench.exe ready - $sizeMB MB"
} else {
    Write-Fail "PA1664Workbench.exe not found"
}

if (-not $hostExists -or -not $workbenchExists) {
    Write-Host ""
    Write-Fail "Build outputs incomplete, cannot continue deployment"
    exit 1
}

# ============================================================================
# Call deployment script
# ============================================================================

Write-Step "Starting package deployment..."

$deployScript = Join-Path $projectRoot 'startup_deploy\deploy-to-startup.ps1'
if (-not (Test-Path $deployScript)) {
    Write-Fail "deploy-to-startup.ps1 not found"
    exit 1
}

$deployArgs = @()
if ($Force) {
    $deployArgs += '-Force'
}
if ($QuickMode) {
    $deployArgs += '-QuickMode'
}

Write-Host ""
Write-Host ("=" * 80) -ForegroundColor Gray

& $deployScript @deployArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Fail "Deployment script failed"
    exit 1
}

# ============================================================================
# Complete
# ============================================================================

Write-Host ""
Write-Title "Build and Deploy Complete!"

Write-Host "Build outputs:" -ForegroundColor Cyan
Write-Host "  bin\PA1664RobotHost.exe" -ForegroundColor White
Write-Host "  bin\PA1664Workbench.exe" -ForegroundColor White

Write-Host ""
Write-Host "Deployment packages:" -ForegroundColor Cyan
Write-Host "  startup_deploy\robot_host\        -> Copy to robot PC" -ForegroundColor White
Write-Host "  startup_deploy\operator_workbench\ -> Copy to operator PC" -ForegroundColor White

if ($QuickMode) {
    Write-Host ""
    Write-Host "Quick Mode Notice:" -ForegroundColor Magenta
    Write-Host "  - EXE files updated only" -ForegroundColor White
    Write-Host "  - Dependencies (DLL) not re-collected" -ForegroundColor White
    Write-Host "  - Use full build if dependencies change" -ForegroundColor White
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Check usage instructions in startup_deploy\README.md" -ForegroundColor White
Write-Host "  2. Copy folders to target PCs" -ForegroundColor White
Write-Host "  3. Configure IP address in config files" -ForegroundColor White
Write-Host "  4. Test startup and install auto-start" -ForegroundColor White

Write-Host ""

