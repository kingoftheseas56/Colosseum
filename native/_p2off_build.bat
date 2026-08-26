@echo off
setlocal
set "ProgramFiles(x86)=C:\Program Files (x86)"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo VCVARS FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -B build-msvc -DCOLOSSEUM_PLAYER2_IN_APP=OFF || (echo CONFIGURE FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
