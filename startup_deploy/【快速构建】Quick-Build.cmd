@echo off
chcp 65001 >nul
cls

echo ============================================================================
echo PA1664 Quick Build - Fast Build for Daily Development
echo ============================================================================
echo.
echo This will:
echo   - Compile both EXE files ^(PA1664RobotHost.exe and PA1664Workbench.exe^)
echo   - Update EXE files ONLY ^(skip DLL collection^)
echo.
echo Estimated time: 1-2 minutes ^(5x faster!^)
echo.
echo NOTE: You must run Full Build at least once before using Quick Build
echo.

pause

echo.
echo Starting QUICK BUILD...
echo.

set "PA1664_BUILD_LOG=%~dp0Quick-Build-log.txt"
set "PA1664_BUILD_SCRIPT=%~dp0build-all.ps1"
echo Build log: %PA1664_BUILD_LOG%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command "$code = 1; Start-Transcript -LiteralPath $env:PA1664_BUILD_LOG -Force | Out-Null; try { & $env:PA1664_BUILD_SCRIPT -QuickMode; if ($null -eq $LASTEXITCODE) { $code = 0 } else { $code = $LASTEXITCODE } } catch { Write-Host $_ -ForegroundColor Red; $code = 1 } finally { Stop-Transcript | Out-Null }; exit $code"
set "BUILD_EXIT_CODE=%errorlevel%"
if "%BUILD_EXIT_CODE%"=="0" (
    echo.
    echo ============================================================================
    echo Quick Build Completed Successfully!
    echo ============================================================================
    echo.
    echo EXE files updated:
    echo   - robot_host\app\PA1664RobotHost.exe
    echo   - operator_workbench\app\PA1664Workbench.exe
    echo.
    echo Dependencies ^(DLL^) were NOT updated.
    echo If you changed dependencies, run Full Build instead.
    echo.
) else (
    echo.
    echo ============================================================================
    echo Build Failed!
    echo ============================================================================
    echo.
    echo If you see "app not found" error, you need to run Full Build first.
    echo.
)

if "%BUILD_EXIT_CODE%"=="0" (
    >> "%PA1664_BUILD_LOG%" echo [CMD] Result: SUCCESS, exit code %BUILD_EXIT_CODE%
) else (
    >> "%PA1664_BUILD_LOG%" echo [CMD] Result: FAILED, exit code %BUILD_EXIT_CODE%
)
echo Build log saved to: %PA1664_BUILD_LOG%
echo.

pause
