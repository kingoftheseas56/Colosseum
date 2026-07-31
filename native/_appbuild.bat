@echo off
setlocal
cd /d "%~dp0"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
