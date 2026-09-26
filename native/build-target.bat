@echo off
REM ============================================================================
REM  Colosseum - incremental single-target MSVC build (dev/TDD loop).
REM  Usage:  native\build-target.bat <cmake-target>
REM  Reuses the build-msvc configuration; loads vcvars so cl/link resolve, then
REM  builds only the requested target. Fast for per-task RED/GREEN cycles.
REM ============================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if "%~1"=="" (echo usage: build-target.bat ^<target^> & exit /b 2)
if not defined COLOSSEUM_BUILD_LOCK_HELD (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-target-locked.ps1" "%~f0" "%~1"
    exit /b !ERRORLEVEL!
)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
"%CMAKE%" --build build-msvc --target %1 || (echo TARGET_BUILD_FAILED & exit /b 1)
echo TARGET_BUILD_OK
