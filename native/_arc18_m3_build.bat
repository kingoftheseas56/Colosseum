@echo off
rem Arc 18 M3 — build the durable-index harness (and deps).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native"
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --parallel 4 --target manga_torrent_index_harness
if %errorlevel% neq 0 exit /b %errorlevel%
echo M3_BUILD_OK
