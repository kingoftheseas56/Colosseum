# BrowserDrawer (Feature 8) sits behind PlayerPage's lazy creation path — a boot smoke
# never instantiates it, so a creation-time QML error (fractional-int literal, bad ref)
# would ship invisibly and the drawer would simply refuse to open. This instantiates it
# headless with real data so the delegates build, and requires LOADER READY.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

$harness = Join-Path $PSScriptRoot "browser_drawer_load_harness.qml"
# QT_FORCE_STDERR_LOGGING is mandatory — without it Qt on Windows swallows console.log.
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String

if ($out -like "*LOADER ERROR*") { throw "BrowserDrawer failed to instantiate. Loader output:`n$out" }
if ($out -notlike "*LOADER READY*") { throw "BrowserDrawer never reached READY. Loader output:`n$out" }

Write-Host "browser drawer load: OK"
