@echo off
REM Arc 18 M2: resolver harness + dependents of the alias-aware Nyaa family.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --parallel 4 --target ^
 manga_torrent_metainfo_resolver_harness ^
 manga_tankoban_logic_harness manga_tankoban_service_harness || (echo BUILD FAILED & exit /b 1)
echo M2_BUILD_OK
