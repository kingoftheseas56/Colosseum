@echo off
REM ============================================================================
REM  THE WAY BACK.
REM
REM  As of 2026-07-26 Colosseum's normal launch uses the NEW video engine
REM  (Player 2). This shortcut runs the SAME app on the OLD player instead -
REM  the mpv one you have been watching things on all along.
REM
REM  Use it if anything about video feels wrong: playback, subtitles, seeking,
REM  audio. Nothing else about the app changes, and nothing is lost by using it.
REM
REM  The old player still has two things the new one does not: Screenshot and
REM  Record GIF (in More controls), and the live TV guide / DVR panels.
REM ============================================================================
title Colosseum (Old Player)
set "CROOT=C:\Users\Suprabha\Desktop\Brotherhood\Colosseum"
cd /d "%CROOT%"
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;C:\tools\mpvqt-feasibility\mpvqt-msvc-install\bin;C:\tools\mpvqt-feasibility\libmpv-prefix\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
REM This is the whole escape hatch: boot the old engine on OpenGL. The graphics
REM backend is chosen once when the app starts, so this has to be a launch
REM choice - it can never be a switch inside the running app.
set "COLOSSEUM_PLAYER1=1"
set "QT_FORCE_STDERR_LOGGING=1"
echo Starting Colosseum on the OLD player (mpv)...
echo.
native\build-msvc\colosseum.exe qml\Main.qml > "%CROOT%\old-player-run.log" 2>&1
echo.
echo Colosseum (Old Player) closed. Log: %CROOT%\old-player-run.log
pause
