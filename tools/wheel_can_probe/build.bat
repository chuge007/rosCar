@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [FAIL] vcvars64
  exit /b 1
)
cd /d "%~dp0"
cl /nologo /utf-8 /EHsc /std:c++17 /O2 /W3 wheel_can_probe.cpp /Fe:wheel_can_probe.exe
if errorlevel 1 (
  echo [FAIL] cl
  exit /b 1
)
echo [OK] wheel_can_probe.exe
