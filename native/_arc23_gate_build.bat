@echo off
setlocal
set "ProgramFiles(x86)=C:\Program Files (x86)"
cd /d "%~dp0"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" || (echo VCVARS_FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target background_work_coordinator_harness background_activity_registry_harness tst_watchparty_ui update_download_harness update_service_harness 2>&1
echo BUILD_EXIT=%ERRORLEVEL%
