@echo off
REM Agent 0 (ZCode) Bundle 8C adoption: build ONLY the 14 new account/sync test
REM targets. -j1 per RAM discipline. Excludes all unrelated targets.
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 FAILED & exit /b 1)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc --parallel 4 --target ^
 tst_account_core tst_sync_core tst_account_identity tst_account_onboarding ^
 tst_account_adoption tst_account_shared_pc tst_sync_inventory ^
 tst_sync_adapter_registry tst_sync_protocol tst_sync_engine ^
 tst_core_sync_adapters tst_history_sync tst_profile_preferences_sync ^
 tst_tankoban_preferences_sync || (echo BUILD FAILED & exit /b 1)
echo B8C_TEST_BUILD_OK
