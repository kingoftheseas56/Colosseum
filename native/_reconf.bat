@echo off
setlocal
set "ProgramFiles(x86)=C:\Program Files (x86)"
cd /d "%~dp0"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B build-msvc -G Ninja -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 >nul || (echo CONFIGURE FAILED & exit /b 1)
echo CONFIGURED
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
