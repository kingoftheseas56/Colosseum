@echo off
set "CROOT=C:\Users\Suprabha\Desktop\Brotherhood\Colosseum"
cd /d "%CROOT%"
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;C:\tools\mpvqt-feasibility\libmpv-prefix\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QSG_INFO=1"
if "%1"=="d3d" set "COLOSSEUM_PLAYER2=1"
native\build-msvc\colosseum.exe tests\d3dbooks\WebTest.qml > "%CROOT%\tests\d3dbooks\arm-%1.log" 2>&1
