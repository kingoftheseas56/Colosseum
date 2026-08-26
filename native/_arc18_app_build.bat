@echo off
rem Arc 18 final - main app build (production HAS_LIBTORRENT path).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cmake --build build-msvc --target colosseum --config Release 2>&1
exit /b %ERRORLEVEL%
