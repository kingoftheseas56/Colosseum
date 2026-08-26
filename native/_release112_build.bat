@echo off
REM Agent 0 sub-exec release-1.1.2 build: colosseum app target, -j1 (RAM-constrained
REM machine). Temporary helper, mirrors _a0_build_app.bat's vcvars64 pattern.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum -j1 || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
