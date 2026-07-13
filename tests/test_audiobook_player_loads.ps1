# AudiobookPlayer (the shared-session REMOTE) is behind a LAZY Loader — boot smokes
# never instantiate it, so a creation-time QML error ships invisibly. This test
# instantiates the page headless against a stubbed audioSession and requires READY.
# Born with Task 4.3 (shared AudiobookSession, 2026-07-13).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}

$harness = Join-Path $PSScriptRoot "audiobook_player_load_harness.qml"
# cmd /c flattens the streams; QT_FORCE_STDERR_LOGGING is mandatory on Windows.
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String

if ($out -notlike "*LOADER READY*") {
    throw "AudiobookPlayer failed to instantiate. Loader output:`n$out"
}

Write-Host "audiobook player load: OK"
