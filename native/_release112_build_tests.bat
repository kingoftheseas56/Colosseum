@echo off
REM Agent 0 sub-exec release-1.1.2: build everything EXCEPT let the known-blocked
REM colosseum link failure (live exe lock) not halt the independent test harnesses.
REM Keep-going (-k 0) so ninja proceeds past that one failed target.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc -j1 -- -k 0
echo BUILD_EXIT=%ERRORLEVEL%
