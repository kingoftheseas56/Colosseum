@echo off
setlocal
set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "BUILD=%TEMP%\colosseum-w06-scheduler-spine-tests"
if exist "%BUILD%" rmdir /s /q "%BUILD%"
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b %errorlevel%
"%CMAKE%" --build "%BUILD%" --config Debug
if errorlevel 1 exit /b %errorlevel%
"%CMAKE%" --build "%BUILD%" --config Debug --target RUN_TESTS
exit /b %errorlevel%
