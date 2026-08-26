@echo off
REM Arc 18 M1: build shared-identity harness + rebuilt dependents.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --parallel 4 --target ^
 manga_volume_identity_harness manga_volume_filepicker_harness ^
 manga_tankoban_logic_harness manga_volume_torrent_harness ^
 manga_tankoban_service_harness || (echo BUILD FAILED & exit /b 1)
echo M1_BUILD_OK
