@echo off
chcp 65001 >nul
cls

echo ============================================================================
echo PA1664 Full Build - Complete Build with All Dependencies
echo ============================================================================
echo.
echo This will:
echo   - Compile both EXE files ^(PA1664RobotHost.exe and PA1664Workbench.exe^)
echo   - Collect ALL dependencies ^(Qt DLL, OpenCV DLL, SDK DLL^)
echo   - Copy plugins and configuration files
echo   - Package to robot_host and operator_workbench folders
echo.
echo Estimated time: 5-10 minutes
echo.

pause

echo.
echo Starting FULL BUILD...
echo.

set "PA1664_BUILD_LOG=%~dp0Full-Build-log.txt"
set "PA1664_BUILD_SCRIPT=%~dp0build-all.ps1"
echo Build log: %PA1664_BUILD_LOG%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command "$code = 1; Start-Transcript -LiteralPath $env:PA1664_BUILD_LOG -Force | Out-Null; try { & $env:PA1664_BUILD_SCRIPT; if ($null -eq $LASTEXITCODE) { $code = 0 } else { $code = $LASTEXITCODE } } catch { Write-Host $_ -ForegroundColor Red; $code = 1 } finally { Stop-Transcript | Out-Null }; exit $code"
set "BUILD_EXIT_CODE=%errorlevel%"
if "%BUILD_EXIT_CODE%"=="0" (
    echo.
    echo ============================================================================
    echo Full Build Completed Successfully!
    echo ============================================================================
    echo.
    echo Deployment packages ready:
    echo   - robot_host\          ^(copy to robot PC^)
    echo   - operator_workbench\  ^(copy to operator PC^)
    echo.
    echo Next steps:
    echo   1. Copy robot_host folder to robot PC
    echo   2. Copy operator_workbench folder to operator PC
    echo   3. Configure IP addresses in config files
    echo   4. Run startup CMD files to test
    echo.
) else (
    echo.
    echo ============================================================================
    echo Build Failed! Check errors above.
    echo ============================================================================
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
