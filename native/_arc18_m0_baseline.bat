@echo off
REM Arc 18 M0 baseline: rebuild the six Tankoban native harness targets from live source.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --parallel 4 --target ^
 manga_tankoban_logic_harness manga_volume_filepicker_harness ^
 manga_volume_index_harness manga_volume_torrent_harness ^
 manga_volume_packer_harness manga_tankoban_service_harness || (echo BUILD FAILED & exit /b 1)
echo M0_BASELINE_BUILD_OK
