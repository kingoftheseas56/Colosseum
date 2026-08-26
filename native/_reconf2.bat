@echo off
setlocal
set "ProgramFiles(x86)=C:\Program Files (x86)"
cd /d "%~dp0"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B build-msvc -DCOLOSSEUM_PLAYER2_IN_APP=ON >nul || (echo CONFIGURE FAILED & exit /b 1)
echo CONFIGURED
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
