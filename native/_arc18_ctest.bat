@echo off
rem Arc 18 final - full ctest over the build tree.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0build-msvc"
ctest -C Release --output-on-failure 2>&1
exit /b %ERRORLEVEL%
