# AudiobookStrip (the reader's shared-session remote) mounts inside BookReader.qml,
# which is behind a LAZY Loader AND hosts a WebEngineView — boot smokes never
# instantiate it and BookReader itself cannot load headless. This test instantiates
# the strip alone against a stubbed audioSession and requires the load AND the
# remote-contract asserts to pass. Born with Task 4.4 (2026-07-13).
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}

$harness = Join-Path $PSScriptRoot "audiobook_strip_load_harness.qml"
# cmd /c flattens the streams; QT_FORCE_STDERR_LOGGING is mandatory on Windows.
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String

if ($out -notlike "*LOADER READY*") {
    throw "AudiobookStrip failed to instantiate. Loader output:`n$out"
}
if ($out -notlike "*STRIP CONTRACT OK*") {
    throw "AudiobookStrip remote contract failed:`n$out"
}

Write-Host "audiobook strip load + contract: OK"
