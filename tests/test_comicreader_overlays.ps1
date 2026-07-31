# Comic Reader — OVERLAYS gate (Task 12; Pages filmstrip added Task 6).
#
# Drives the overlay surfaces offscreen through comicreader_overlays_harness.qml and asserts their
# behaviour. Slice 1: ComicReaderSettingsSheet's DISPLAY section (mode + direction on the shell's
# existing persisted seams), open/close/dismiss, and the click-swallower law. Slice 2:
# ComicReaderPagesOverlay — the temporary Pages filmstrip (virtualization, real centring geometry,
# RTL mirroring of the visuals only, single-fire jump, dismiss-without-moving, thumbnail tier).
# The remaining temporary surfaces (Image, Layout, Loupe) extend this harness in Tasks 7-9.
#
# qml.exe is located exactly as every sibling tests/test_*.ps1 does (hardcoded Qt path); -I
# tests/qmlmock mirrors the sibling harnesses (harmless here — the overlays need no mock module).

$ErrorActionPreference = "Stop"

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

# --- static assertions over the overlay sources themselves (PowerShell can read files; qml.exe
#     cannot read arbitrary local files reliably, so the file-level laws live here). ---
$overlays = @(
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderSettingsSheet.qml"),
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderPagesOverlay.qml"),
    (Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderImagePopover.qml")
)
foreach ($overlay in $overlays) {
    if (!(Test-Path -LiteralPath $overlay)) {
        Write-Host "FAIL: overlay source not found at $overlay"
        exit 1
    }
    # NO guided import/reference in the overlays (Guided is frozen, owned elsewhere)
    $guidedHits = Select-String -LiteralPath $overlay -Pattern "guided" -SimpleMatch -CaseSensitive:$false
    if ($guidedHits) {
        Write-Host "FAIL: $overlay must contain NO guided reference; found:"
        $guidedHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
        exit 1
    }
}

# --- the filmstrip must never ask for a full-resolution page. The behavioural half of this is in
#     the harness (it asserts the tier actually requested); this is the source-level backstop, so an
#     hq/preview request cannot be reintroduced in a code path the offscreen run does not reach. ---
$film = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderPagesOverlay.qml"
$hqHits = Select-String -LiteralPath $film -Pattern '\.imageUrl\s*\(' |
    Where-Object { $_.Line.TrimStart() -notlike '//*' -and $_.Line -notmatch '"thumbnail"' }
if ($hqHits) {
    Write-Host "FAIL: $film must request the 'thumbnail' tier on every imageUrl call; found:"
    $hqHits | ForEach-Object { Write-Host ("  line " + $_.LineNumber + ": " + $_.Line.Trim()) }
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
