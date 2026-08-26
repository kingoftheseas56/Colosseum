@echo off
REM Slice 4 prep: reconfigure (picks up new qrc) + build colosseum app target.
cd /d "%~dp0\build-msvc"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" .. > ..\_slice4_cmake.log 2>&1
if errorlevel 1 (echo CMAKE_RECONFIG_FAILED & type ..\_slice4_cmake.log & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build . --target colosseum --config Debug > ..\_slice4_appbuild.log 2>&1
if errorlevel 1 (echo APP_BUILD_FAILED & type ..\_slice4_appbuild.log & exit /b 1)
echo APP_BUILD_OK
