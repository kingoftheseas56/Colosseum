# UniverseHallPage is behind a lazy Loader — this test actually instantiates the
# page headless and requires LOADER READY (the lazy-page load gate).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}

$harness = Join-Path $PSScriptRoot "universe_hall_load_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String

if ($out -notlike "*LOADER READY*") {
    throw "UniverseHallPage failed to instantiate. Loader output:`n$out"
}

Write-Host "universe hall page load: OK"
