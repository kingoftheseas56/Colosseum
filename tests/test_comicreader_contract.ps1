# Comic Reader — PUBLIC CALLER CONTRACT gate (Task 1 oracle).
#
# Drives the REAL qml/MangaReader.qml offscreen through comicreader_contract_harness.qml and
# asserts the caller contract survives: input properties, signals, the injected store calls, and
# the manga/comic/tankoban progress namespace. This runs against the CURRENT (old) reader and
# MUST pass now; it re-runs UNCHANGED after the Task 13 cutover to prove no caller broke.
#
# Contract doc: docs/superpowers/handoffs/2026-07-23-comicreader-public-contract.md
#
# qml.exe is located exactly as every sibling tests/test_*.ps1 does (hardcoded Qt path); the
# mock module path (-I tests/qmlmock) mirrors the guided harness so the reader's guided Loader
# resolves cleanly under plain qml.exe.

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_contract_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_CONTRACT_OK")) {
    Write-Host "FAIL: comic reader contract offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_CONTRACT_OK"
