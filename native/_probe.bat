@echo off
rem Local probe runner (untracked, same shape as _reconf2.bat). Three things this exists for:
rem   * git-bash cannot hand a Windows GUI exe a usable DLL search path;
rem   * colosseum.exe is a GUI-subsystem binary whose stdio only lands in a file when CMD opens it;
rem   * the worktree build stages only the FFmpeg DLLs - MpvQt.dll / mpv-2.dll still come from the
rem     MAIN TREE's native\build-msvc, so that directory has to be on PATH or the exe dies with
rem     STATUS_DLL_NOT_FOUND (0xC0000135) before main() and looks exactly like a probe that hung.
rem Usage: _probe.bat <qml> <media> <mode>   -> writes native/build-msvc/_probe.log
setlocal
set "MAINTREE=C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\native\build-msvc"
set "PATH=C:\Qt\6.11.1\msvc2022_64\bin;%~dp0build-msvc;%MAINTREE%;%PATH%"
set "COLOSSEUM_PLAYER2=1"
set "QSG_NO_VSYNC=1"
set "QT_FORCE_STDERR_LOGGING=1"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"
cd /d "%~dp0.."
"%~dp0build-msvc\colosseum.exe" %* > "%~dp0build-msvc\_probe.log" 2>&1
echo PROBE EXIT=%ERRORLEVEL%
