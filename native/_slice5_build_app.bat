@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo VCVARS_FAILED & exit /b 1)
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum 2>&1
echo BUILD_EXIT=%ERRORLEVEL%
