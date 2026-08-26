@echo off
REM Agent 0 scoped build: the colosseum app target ONLY (skips brothers' broken
REM test targets in the working tree, e.g. tst_vault_forensics). Temporary helper.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --target colosseum comick_catalog_parse_harness || (echo BUILD FAILED & exit /b 1)
echo BUILD_OK
