@echo off
REM ============================================================================
REM  Colosseum runtime deploy — makes colosseum.exe truly standalone.
REM
REM  Copies the full Qt runtime (DLLs, plugins, QML modules, and the WebEngine
REM  process + resources the bat's PATH used to paper over) NEXT TO the exe in
REM  native\build-msvc\ (gitignored). Run ONCE, and again after a Qt upgrade.
REM  The app loads qml\ live from the repo — this deploys the RUNTIME only.
REM
REM  Desktop shortcut (regen):  powershell -NoProfile -Command ^
REM    "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Colosseum.lnk');" ^
REM    "$s.TargetPath='%~dp0build-msvc\colosseum.exe'; $s.WorkingDirectory='%~dp0..'; $s.Save()"
REM ============================================================================
setlocal
cd /d "%~dp0.."
"C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe" --qmldir qml native\build-msvc\colosseum.exe || (echo DEPLOY FAILED & exit /b 1)
echo DEPLOY_OK
