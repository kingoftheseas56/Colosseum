@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_reddit_captures.ps1"
if errorlevel 1 pause
endlocal
