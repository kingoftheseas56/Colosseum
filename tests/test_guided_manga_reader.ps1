# Guided — FREEZE gate (converted at the Task 13 cutover, 2026-07-25).
#
# This gate used to assert that Guided WORKED as the fifth style of the old MangaReader. That reader
# is gone: qml/MangaReader.qml now delegates to the from-scratch Comic Reader, which has no Guided
# path. So the gate is inverted, and it is careful about which way:
#
#   Guided is FROZEN, not deleted.
#
# Frozen means two things, and this file asserts both, because either one alone is a lie:
#   1. The implementation is STILL ON DISK and whole — native/guided/ (the camera controller, panel
#      detector seam, planner, map store) and qml/guided/ (viewport, controls, host wrapper). Agent 0
#      paid for the ONNX detector seam and the export half; a "cleanup" that quietly deletes them
#      throws that away and makes thawing a rewrite instead of a re-wire.
#   2. The production reader CANNOT REACH IT. No guided import, no guided mode, no guided state — so
#      nothing offers Hemanth a Guided option that would land on a surface nobody is maintaining.
#
# It deliberately prints GUIDED_FROZEN_OK, never a "guided works" sentinel: this gate makes no claim
# whatsoever about Guided behaving. tests/guided_manga_reader_harness.qml is left on disk unused —
# it is the behavioural recipe to restore if Guided is ever thawed against the new reader.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$message) {
    Write-Host "FAIL: $message"
    exit 1
}

# ---- 1. the implementation is still on disk, and whole ----
$nativeGuided = @(
    "native/guided/GuidedCameraController.h", "native/guided/GuidedCameraController.cpp",
    "native/guided/PanelDetector.h", "native/guided/PanelDetectorOnnx.cpp",
    "native/guided/PanelPlanner.cpp", "native/guided/PanelMapStore.cpp",
    "native/guided/PanelAnalysisService.cpp", "native/guided/GuidedTypes.cpp"
)
$qmlGuided = @(
    "qml/guided/GuidedViewport.qml", "qml/guided/GuidedControls.qml",
    "qml/guided/GuidedCameraHost.qml", "qml/guided/GuidedAnalysisDetails.qml"
)
foreach ($f in ($nativeGuided + $qmlGuided)) {
    if (!(Test-Path -LiteralPath (Join-Path $root $f))) {
        Fail "Guided is FROZEN, not deleted - missing $f. Restore it; do not prune the guided tree."
    }
}

# The host wrapper stays the single place that imports the native module (the isolation that let the
# rest of the reader build with the seam OFF). If that moves, thawing gets harder.
$hostwrap = Get-Content -Raw -LiteralPath (Join-Path $root "qml/guided/GuidedCameraHost.qml")
if (-not $hostwrap.Contains('import Colosseum.Guided')) {
    Fail "qml/guided/GuidedCameraHost.qml must remain the one place importing Colosseum.Guided"
}

# ---- 2. the production reader cannot reach it ----
$reader = Get-Content -Raw -LiteralPath (Join-Path $root "qml/MangaReader.qml")
foreach ($needle in @('import "guided"', 'import Colosseum.Guided', 'GuidedViewport', 'GuidedControls',
                      'guidedCameraLoader', 'enterGuided', '{v:"guided"')) {
    if ($reader.Contains($needle)) {
        Fail "the production reader must not reach Guided - found '$needle' in qml/MangaReader.qml"
    }
}

# The whole comic reader tree, not just the entry file: a guided reference anywhere in it is a path
# back to an unmaintained surface.
$crDir = Join-Path $root "qml/comicreader"
$leaks = @()
$leaks += Get-ChildItem -LiteralPath $crDir -File | Where-Object { $_.Extension -in ".qml", ".js" } |
          ForEach-Object { Select-String -LiteralPath $_.FullName -Pattern "guided" -SimpleMatch -CaseSensitive:$false }
if ($leaks) {
    Write-Host "FAIL: the comic reader tree must contain NO guided reference; found:"
    $leaks | ForEach-Object { Write-Host ("  " + $_.Path + ":" + $_.LineNumber + ": " + $_.Line.Trim()) }
    exit 1
}

# The mode identity itself: three modes, and none of them is Guided. The behavioural half of this
# (asking the picker for "guided" and getting something else) is pinned in the migration harness.
$state = Get-Content -Raw -LiteralPath (Join-Path $root "qml/comicreader/ComicReaderState.js")
foreach ($m in @('"manga"', '"comic"', '"strip"')) {
    if (-not $state.Contains($m)) { Fail "the reading-mode identity must still offer $m" }
}

Write-Host "GUIDED_FROZEN_OK"
