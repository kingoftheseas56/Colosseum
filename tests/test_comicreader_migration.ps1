# Comic Reader — MIGRATION gate (Task 13, the cutover).
#
# Drives tests/comicreader_migration_acceptance.qml, which loads the PRODUCTION reader path
# (qml/MangaReader.qml) and asserts the from-scratch Comic Reader is what answers: the full Task 1
# caller contract, the C++ backend seam + image://comicreader/ pages, both modes selectable with
# Guided unreachable, a byte-identical Continue payload, and memory that survives a full reader
# recreation.
#
# Before the cutover this FAILS (the old reader answers the filename). That failure is the point:
# it is what proves the gate can tell the two readers apart.
#
# Plus a static assertion the harness cannot make about itself: after the cutover MangaReader.qml
# must be a THIN WRAPPER — no state, no behaviour — or the "callers untouched" promise is being
# kept by a second implementation instead of by delegation.

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$reader = Join-Path $PSScriptRoot "../qml/MangaReader.qml"
if (!(Test-Path -LiteralPath $reader)) {
    Write-Host "FAIL: production reader not found at $reader"
    exit 1
}

# --- static: the production reader is a thin delegation, not a second implementation ---
$readerLines = (Get-Content -LiteralPath $reader | Where-Object { $_.Trim() -ne "" -and $_.Trim() -notmatch '^//' }).Count
if ($readerLines -gt 12) {
    Write-Host "FAIL: qml/MangaReader.qml has $readerLines code lines - after the cutover it must be a"
    Write-Host "      thin ComicReaderShell wrapper (no state, no behaviour). A fat file here means the"
    Write-Host "      caller contract is being upheld by a second implementation, not by delegation."
    exit 1
}
if (!(Select-String -LiteralPath $reader -Pattern "ComicReaderShell" -SimpleMatch -Quiet)) {
    Write-Host "FAIL: qml/MangaReader.qml must delegate to ComicReaderShell"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_migration_acceptance.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_MIGRATION_OK")) {
    Write-Host "FAIL: comic reader migration acceptance (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_MIGRATION_OK"
