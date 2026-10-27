# ============================================================================
# PA1664 一键编译并部署工具
# 功能：自动编译 Host 和 Workbench，然后打包到 startup_deploy
# ============================================================================

param(
    [switch]$SkipCompile,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

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

function Write-Error {
    param([string]$Text)
    Write-Host "  [FAIL] $Text" -ForegroundColor Red
}

function Write-Warning {
    param([string]$Text)
    Write-Host "  [WARN] $Text" -ForegroundColor Yellow
}

# ============================================================================
# 第一步：查找编译工具
# ============================================================================

Write-Title "PA1664 一键编译并部署工具"

if (-not $SkipCompile) {
    Write-Step "查找编译工具..."
    
    # 查找 qmake
    $qmake = $null
    $qmakePaths = @(
        $env:QMAKE,
        'D:\qt5.15.2\6.8.3\msvc2022_64\bin\qmake.exe',
        'D:\QT\6.8.3\msvc2022_64\bin\qmake.exe',
        'C:\Qt\6.8.3\msvc2022_64\bin\qmake.exe'
    )
    
    foreach ($path in $qmakePaths) {
        if ($path -and (Test-Path $path)) {
            $qmake = $path
            Write-Success "找到 qmake: $qmake"
            break
        }
    }
    
    if (-not $qmake) {
        # 尝试从 PATH 查找
        $qmakeInPath = Get-Command qmake -ErrorAction SilentlyContinue
        if ($qmakeInPath) {
            $qmake = $qmakeInPath.Source
            Write-Success "找到 qmake (PATH): $qmake"
        }
    }
    
    if (-not $qmake) {
        Write-Error "未找到 qmake，请安装 Qt 6.8.3 或设置 QMAKE 环境变量"
        Write-Host ""
        Write-Host "解决方案：" -ForegroundColor Yellow
        Write-Host "  1. 安装 Qt 6.8.3 MSVC 2022 64-bit" -ForegroundColor White
        Write-Host "  2. 或手动在 Qt Creator 中编译项目" -ForegroundColor White
        Write-Host "  3. 然后运行: .\one-click-deploy.ps1 -SkipCompile" -ForegroundColor White
        exit 1
    }
    
    # 查找 jom 或 nmake
    $builder = $null
    $jomPaths = @(
        'D:\qt5.15.2\Tools\QtCreator\bin\jom\jom.exe',
        'C:\Qt\Tools\QtCreator\bin\jom\jom.exe'
    )
    
    foreach ($path in $jomPaths) {
        if (Test-Path $path) {
            $builder = $path
            Write-Success "找到 jom: $builder"
            break
        }
    }
    
    if (-not $builder) {
        # 尝试 nmake
        $nmakeInPath = Get-Command nmake -ErrorAction SilentlyContinue
        if ($nmakeInPath) {
            $builder = 'nmake'
            Write-Success "找到 nmake"
        }
    }
    
    if (-not $builder) {
        Write-Error "未找到 jom 或 nmake"
        Write-Host ""
        Write-Host "解决方案：" -ForegroundColor Yellow
        Write-Host "  1. 打开 VS 2022 Developer Command Prompt" -ForegroundColor White
        Write-Host "  2. 在该命令行中运行此脚本" -ForegroundColor White
        Write-Host "  3. 或手动在 Qt Creator 中编译" -ForegroundColor White
        exit 1
    }
}

# ============================================================================
# 第二步：编译 PA1664RobotHost
# ============================================================================

$binDir = Join-Path $projectRoot 'bin'
$hostExe = Join-Path $binDir 'PA1664RobotHost.exe'
$workbenchExe = Join-Path $binDir 'PA1664Workbench.exe'

if (-not $SkipCompile) {
    Write-Step "编译 PA1664RobotHost.exe..."
    
    $hostPro = Join-Path $projectRoot 'PA1664RobotHost.pro'
    
    Push-Location $projectRoot
    try {
        Write-Host "  运行 qmake..." -ForegroundColor Gray
        & $qmake $hostPro 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "qmake 失败"
        }
        
        Write-Host "  运行构建..." -ForegroundColor Gray
        if ($builder -eq 'nmake') {
            & nmake 2>&1 | Out-Null
        } else {
            & $builder 2>&1 | Out-Null
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "构建失败"
        }
        
        if (Test-Path $hostExe) {
            $size = [Math]::Round((Get-Item $hostExe).Length / 1MB, 2)
            Write-Success "PA1664RobotHost.exe 编译成功 ($size MB)"
        } else {
            throw "编译后未找到 exe 文件"
        }
    }
    catch {
        Write-Error "PA1664RobotHost 编译失败: $_"
        Write-Host ""
        Write-Host "建议：在 Qt Creator 中手动编译，然后运行: .\one-click-deploy.ps1 -SkipCompile" -ForegroundColor Yellow
        Pop-Location
        exit 1
    }
    finally {
        Pop-Location
    }
    
    # ============================================================================
    # 第三步：编译 PA1664Workbench（如果需要）
    # ============================================================================
    
    if (-not (Test-Path $workbenchExe)) {
        Write-Step "编译 PA1664Workbench.exe..."
        
        $workbenchPro = Join-Path $projectRoot 'PA1664Workbench.pro'
        
        Push-Location $projectRoot
        try {
            Write-Host "  运行 qmake..." -ForegroundColor Gray
            & $qmake $workbenchPro 2>&1 | Out-Null
            
            Write-Host "  运行构建..." -ForegroundColor Gray
            if ($builder -eq 'nmake') {
                & nmake 2>&1 | Out-Null
            } else {
                & $builder 2>&1 | Out-Null
            }
            
            if (Test-Path $workbenchExe) {
                $size = [Math]::Round((Get-Item $workbenchExe).Length / 1MB, 2)
                Write-Success "PA1664Workbench.exe 编译成功 ($size MB)"
            }
        }
        catch {
            Write-Warning "PA1664Workbench 编译失败，但可能已存在旧版本"
        }
        finally {
            Pop-Location
        }
    } else {
        $size = [Math]::Round((Get-Item $workbenchExe).Length / 1MB, 2)
        Write-Success "PA1664Workbench.exe 已存在 ($size MB)"
    }
}

# ============================================================================
# 第四步：验证编译产物
# ============================================================================

Write-Step "验证编译产物..."

$hostExists = Test-Path $hostExe
$workbenchExists = Test-Path $workbenchExe

if (-not $hostExists) {
    Write-Error "PA1664RobotHost.exe 不存在"
}
if (-not $workbenchExists) {
    Write-Error "PA1664Workbench.exe 不存在"
}

if (-not $hostExists -or -not $workbenchExists) {
    Write-Host ""
    Write-Error "编译产物不完整，无法继续部署"
    exit 1
}

Write-Success "所有编译产物已就绪"

# ============================================================================
# 第五步：调用部署脚本
# ============================================================================

Write-Step "开始自动打包部署..."

$deployScript = Join-Path $projectRoot 'deploy-to-startup.ps1'
if (-not (Test-Path $deployScript)) {
    Write-Error "找不到 deploy-to-startup.ps1"
    exit 1
}

$deployArgs = @()
if ($Force) {
    $deployArgs += '-Force'
}

Write-Host ""
Write-Host ("=" * 80) -ForegroundColor Gray

& $deployScript @deployArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Error "部署脚本执行失败"
    exit 1
}

# ============================================================================
# 完成
# ============================================================================

Write-Host ""
Write-Title "部署完成！"

Write-Host "部署包位置：" -ForegroundColor Cyan
Write-Host "  startup_deploy\robot_host\        → 复制到小车电脑" -ForegroundColor White
Write-Host "  startup_deploy\operator_workbench\ → 复制到操作电脑" -ForegroundColor White

Write-Host ""
Write-Host "下一步操作：" -ForegroundColor Cyan
Write-Host "  1. 查看 DEPLOY_GUIDE.md 了解部署步骤" -ForegroundColor White
Write-Host "  2. 复制对应文件夹到目标电脑" -ForegroundColor White
Write-Host "  3. 修改配置文件中的 IP 地址" -ForegroundColor White
Write-Host "  4. 测试启动并安装开机自启动" -ForegroundColor White

Write-Host ""
