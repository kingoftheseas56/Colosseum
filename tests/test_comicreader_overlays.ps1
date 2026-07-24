# Comic Reader — OVERLAYS gate (Task 12).
#
# Drives the overlay surfaces offscreen through comicreader_overlays_harness.qml and asserts their
# behaviour. Slice 1: ComicReaderSettingsSheet's DISPLAY section (mode + direction on the shell's
# existing persisted seams), open/close/dismiss, and the click-swallower law. More sections
# (night veil, mode-specific rows, tool grid, danger row) and the navigator / end-card / tool
# overlays extend this harness in later slices.
#
# qml.exe is located exactly as every sibling tests/test_*.ps1 does (hardcoded Qt path); -I
# tests/qmlmock mirrors the sibling harnesses (harmless here — the overlays need no mock module).

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

# --- static assertion: NO guided import/reference in the overlays (Guided is frozen, owned elsewhere) ---
$overlay = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderSettingsSheet.qml"
if (!(Test-Path -LiteralPath $overlay)) {
    Write-Host "FAIL: settings sheet not found at $overlay"
    exit 1
}
$guidedHits = Select-String -LiteralPath $overlay -Pattern "guided" -SimpleMatch -CaseSensitive:$false
if ($guidedHits) {
    Write-Host "FAIL: $overlay must contain NO guided reference; found:"
    $guidedHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness  = Join-Path $PSScriptRoot "comicreader_overlays_harness.qml"
$mockPath = Join-Path $PSScriptRoot "qmlmock"

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen -I $mockPath $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "COMICREADER_OVERLAYS_OK")) {
    Write-Host "FAIL: comic reader overlays offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "COMICREADER_OVERLAYS_OK"
