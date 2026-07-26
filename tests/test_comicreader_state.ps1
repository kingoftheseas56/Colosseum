# Comic Reader — pure state library gate (Task 8 oracle).
#
# Drives tests/comicreader_state_harness.qml offscreen against qml/comicreader/ComicReaderState.js
# (a `.pragma library` — no QML context access, every decision is a function of its arguments)
# and asserts every pure decision the Task 9 shell will lean on: progressKind namespace,
# entryIndex/nextEntry/previousEntry crossing, completion, the load-bearing progressPayload
# shape (must match the Task 1 §4.1 recorded payload exactly), defaultDirection, shouldAcquire,
# defaultMode.
#
# Contract doc: docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "comicreader_state_harness.qml"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_STATE_OK")) {
    Write-Host "FAIL: comic reader state offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_STATE_OK"
