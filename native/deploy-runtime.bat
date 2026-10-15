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
REM Felt-speed Stage 0: qwebp.dll never rode windeployqt (the app doesn't link it — it's a
REM runtime-discovered plugin), so WebP covers decoded ONLY on machines where someone
REM hand-dropped the dll into the Qt install. Bundle it explicitly; fail loudly if absent.
copy /Y "C:\Qt\6.11.1\msvc2022_64\plugins\imageformats\qwebp.dll" "native\build-msvc\imageformats\qwebp.dll" || (echo WEBP BUNDLE FAILED & exit /b 1)
echo WEBP_OK
echo DEPLOY_OK
