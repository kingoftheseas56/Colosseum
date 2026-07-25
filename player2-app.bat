@echo off
REM ============================================================================
REM  Colosseum with PLAYER 2 as the video backend (Task 17, opt-in).
REM
REM  This runs the Player-2-enabled build from THIS worktree. The normal app is
REM  untouched: Player 2 only exists in a build made with COLOSSEUM_PLAYER2_IN_APP=ON
REM  AND with player.ini [backend] usePlayer2=true (both set up here already).
REM
REM  To go back to the normal player without rebuilding: set usePlayer2=false in
REM  player.ini next to this file, or just run the usual Colosseum.
REM ============================================================================
cd /d "%~dp0"
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;C:\tools\mpvqt-feasibility\libmpv-prefix\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
set "COLOSSEUM_DEV=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QML_DISABLE_DISK_CACHE=1"
native\build-msvc\colosseum.exe qml\Main.qml
