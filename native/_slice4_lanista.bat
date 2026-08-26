@echo off
cd /d "%~dp0\build-msvc"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target lanista --config Debug > ..\_slice4_lanista.log 2>&1
echo LANISTA_BUILD_%errorlevel%
