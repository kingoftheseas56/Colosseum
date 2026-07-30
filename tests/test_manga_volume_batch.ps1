# Contract + logic gate for MANGA VOLUME BATCH DOWNLOAD ("Download next 10").
#
# Spec:  Brotherhood/docs/superpowers/specs/2026-07-30-colosseum-manga-volume-batch-download-design.md
# Plan:  Brotherhood/docs/superpowers/plans/2026-07-30-manga-volume-batch-download.md
# Probe: tests/manga_volume_pack_probe.md  (why the torrent-pack route exists at all)
#
# Three layers, cheapest first:
#   1. PURE LOGIC   — node drives the .pragma library's paging + selection maths.
#   2. GREP SHAPE   — the load-bearing wiring strings are present. A green grep
#                     proves a string EXISTS, never that it behaves.
#   3. OFFSCREEN    — qml.exe drives the real MangaTankobanLibrary and the real
#                     MangaTankobanSourcesPage against a fake service, and must
#                     print its sentinel. This is the layer that proves behaviour.
#
# The final pixels are Hemanth's eyes-on; this file pins logic + shape only.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}
function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        Write-Host "FAIL: $message"
        exit 1
    }
}

# ── 1. Pure logic must be green first ────────────────────────────────────────
Push-Location $root
try {
    & node "tests/manga_volume_batch_test.mjs" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: pure batch logic (tests/manga_volume_batch_test.mjs)"
        & node "tests/manga_volume_batch_test.mjs"
        exit 1
    }
} finally {
    Pop-Location
}

# ── 2. Wiring shape ──────────────────────────────────────────────────────────
$library = Read-RepoFile "qml/MangaTankobanLibrary.qml"
$series  = Read-RepoFile "qml/MangaSeries.qml"
$picker  = Read-RepoFile "qml/MangaTankobanSourcesPage.qml"
$svcHdr  = Read-RepoFile "native/engine/MangaTankobanService.h"
$svcCpp  = Read-RepoFile "native/engine/MangaTankobanService.cpp"

# The shelf pages, and can ask for a batch.
Assert-Contains $library 'Vol.pageGroups' "the shelf must page through MangaVolumes.js"
Assert-Contains $library 'Vol.nextBatch'  "the shelf must derive the next batch through MangaVolumes.js"
Assert-Contains $library 'signal batchRequested' "the shelf must be able to ask for a batch"
Assert-Contains $library 'unavailableNumbers' "a batch must know what it may not ask for"
Assert-Contains $library 'function unownedIn' "each page must filter to what it can actually fetch"
Assert-Contains $library 'function cancelRemaining' "acceptance 11 needs a batch-level cancel"
Assert-Contains $library 'inFlightIds' "cancel remaining must target only unfinished volumes"

# The series page turns numbers into ids, re-checks ownership, and opens ONE picker.
Assert-Contains $series 'onBatchRequested' "the series page must handle a batch request"
Assert-Contains $series 'function _requestBatch' "numbers -> ids must live in one place"
Assert-Contains $series 'function _openSources' "single and batch must share one picker-open path"
Assert-Contains $series '!== "ready"' "an owned volume must never be re-downloaded"
Assert-Contains $series 'volumeNumbers' "the picker needs the batch's numbers to filter coverage"

# The batch ACTIONS live on the shelf's own ledger header, in Theatre's
# "Download season" position (eyes-on 2026-07-31). Anchoring them to the series
# header's right edge pushed them off the window.
Assert-Contains $library 'volumeLedgerHeader' "the shelf needs Theatre's ledger header"
Assert-Contains $library 'pageDownloadAction' "the download action must sit where Download season sits"
Assert-Contains $library 'cancelRemainingAction' "the Cancel remaining control must be wired"
Assert-Contains $library 'width: listCol.width - 2 * theme.margin' `
    "the ledger header must be inset both sides or its right-anchored action clips"
# The trap that caused the clipping: a MouseArea parented to a positioner is laid
# out as an item and inflates it. Both actions must be Rectangles, not Rows.
if ($library -match 'Row\s*\{[^}]*id:\s*(primaryBatch|pageBatchRow)') {
    Write-Host "FAIL: a batch action must not be a positioner containing a MouseArea"
    exit 1
}

# The picker applies ONE route across EVERY volume, and offers only covering packs.
Assert-Contains $picker 'batchIds' "the picker must know the batch"
Assert-Contains $picker 'function coversBatch' "only releases covering the whole ask may be offered"
Assert-Contains $picker 'function rowsForBatch' "batch rows must be filtered and sorted"
Assert-Contains $picker 'downloadNyaaBatch' "the Nyaa route must dispatch as ONE batch call"
Assert-Contains $picker 'compileWeebCentral' "the WeebCentral route must remain available"
# The WeebCentral loop is the contract: one compile per volume, not one per batch.
Assert-Contains $picker 'for (var i = 0; i < ids.length; i++) s.compileWeebCentral' `
    "the WeebCentral route must LOOP over every volume of the batch"

# The service exposes a batch entry point that validates before it acts.
Assert-Contains $svcHdr 'downloadNyaaBatch' "the service must expose a batch entry point"
Assert-Contains $svcCpp 'not among the cached search candidates' `
    "the batch entry point must still refuse an unvalidated magnet"

# Fences from the plan: none of this may touch Main.qml or Theatre's sheet.
$main = Read-RepoFile "qml/Main.qml"
if ($main.Contains('batchRequested') -or $main.Contains('downloadNyaaBatch')) {
    Write-Host "FAIL: Main.qml is shared and must not carry batch wiring"
    exit 1
}
$theatreSheet = Join-Path $root "qml/SourcesSheet.qml"
if (Test-Path -LiteralPath $theatreSheet) {
    $sheet = Read-RepoFile "qml/SourcesSheet.qml"
    if ($sheet.Contains('downloadNyaaBatch')) {
        Write-Host "FAIL: Theatre's SourcesSheet.qml must not be touched by manga batching"
        exit 1
    }
}

# ── 3. Offscreen behaviour ───────────────────────────────────────────────────
$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "manga_volume_batch_harness.qml"
# qml.exe emits benign warnings (font dir) on stderr; don't let ErrorActionPreference=Stop
# turn a native-command stderr line into a terminating error before we read the verdict.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP
if ($code -ne 0 -or ($output -notmatch "MANGA_VOLUME_BATCH_OK")) {
    Write-Host "FAIL: offscreen batch harness (exit $code)"
    Write-Host $output
    exit 1
}

Write-Host "manga volume batch: OK"
