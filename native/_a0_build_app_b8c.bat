@echo off
REM Agent 0 (ZCode) Bundle 8C adoption: verify the MAIN app target still
REM compiles with the store seam additions (ProgressStore/CollectionStore/
REM AudioPairingStore headers are compiled into main.cpp).
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --parallel 4 --target colosseum || (echo BUILD FAILED & exit /b 1)
echo APP_BUILD_OK
