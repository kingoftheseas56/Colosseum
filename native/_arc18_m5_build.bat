@echo off
rem Arc 18 M5 - build the service harness (index-first lookup).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cmake --build build-msvc --target manga_tankoban_service_harness --config Release 2>&1
exit /b %ERRORLEVEL%
