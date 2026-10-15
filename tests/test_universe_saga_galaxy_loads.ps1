# SagaUniversePage + GalaxyUniversePage + CinematicPage ride the universe layer's lazy
# Loader — this test instantiates ALL headless and requires LOADER READY (the lazy-page
# load gate). Added 2026-07-13: saga/galaxy grew the comics door, cinematic grew the
# television act.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}

$env:QT_FORCE_STDERR_LOGGING = "1"

foreach ($h in @("universe_saga_load_harness.qml", "universe_galaxy_load_harness.qml")) {
    $harness = Join-Path $PSScriptRoot $h
    $out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String
    if ($out -notlike "*LOADER READY*") {
        throw "$h failed to instantiate its page. Loader output:`n$out"
    }
}

Write-Host "universe saga + galaxy page loads: OK"
