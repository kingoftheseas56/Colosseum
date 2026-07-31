@echo off
setlocal
set "ProgramFiles(x86)=C:\Program Files (x86)"
cd /d "%~dp0"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-player2 --target player2_subtitle_schedule_test player2_subtitle_timing_test player2_subtitle_image_test >nul 2>&1 || (echo BUILD FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir build-player2/player2 -R "subtitle" --output-on-failure 2>&1
