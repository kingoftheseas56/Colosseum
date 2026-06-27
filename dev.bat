@echo off
cd /d "%~dp0"
echo Colosseum - dev loop (live reload on save). Edit any qml\*.qml, hit save, watch it refresh.
echo.
set "PATH=C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
set "COLOSSEUM_DEV=1"
set "QT_FORCE_STDERR_LOGGING=1"
REM Never run a stale compiled copy of an edited .qml — always compile from source in the dev loop.
set "QML_DISABLE_DISK_CACHE=1"
native\build\colosseum.exe qml\Main.qml
pause
