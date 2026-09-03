@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\Qt\Tools\Ninja;%PATH%"
set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "CTEST=C:\Qt\Tools\CMake_64\bin\ctest.exe"
set "SRC=%~dp0."
set "BLD=%~dp0build"
"%CMAKE%" -S "%SRC%" -B "%BLD%" -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
if errorlevel 1 exit /b %errorlevel%
"%CMAKE%" --build "%BLD%"
if errorlevel 1 exit /b %errorlevel%
"%CTEST%" --test-dir "%BLD%" --output-on-failure
