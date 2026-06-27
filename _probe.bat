@echo off
cd /d "%~dp0"
set "PATH=C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
set "QT_FORCE_STDERR_LOGGING=1"
native\build\colosseum.exe qml\_maintest.qml
