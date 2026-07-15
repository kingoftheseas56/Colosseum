$ErrorActionPreference = "Stop"

# Subtitle popover click-through + confirm-before-close (Agent 0 on A4's behalf, 2026-07-15).
# Pairs a BEHAVIORAL headless harness (drives the real SubtitleMenu selection state machine) with
# structural shape guards — interaction coverage, not source-text alone (DoD requirement).

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# ---- 1. BEHAVIORAL: exactly-one-emit, stay-open-while-pending, close-on-confirm, error-on-fail ----
$harness = Join-Path $PSScriptRoot "subtitle_menu_interaction_harness.qml"
$null = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1"
if ($LASTEXITCODE -ne 0) {
    throw "SubtitleMenu interaction harness FAILED (exit $LASTEXITCODE) - confirm-before-close / single-emit / stay-open-on-fail regressed."
}

# ---- 2. SHAPE: both popovers hosted on the full-screen chrome layer (rows above the dock click) ----
$sub = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw
$audio = Get-Content (Join-Path $root "qml/AudioMenu.qml") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

Assert-Contains $sub   "property Item overlayParent"   "SubtitleMenu must accept an overlayParent (chrome host)."
Assert-Contains $sub   "menu.mapToItem(overlayParent"  "SubtitleMenu must position itself in overlay coordinates."
Assert-Contains $audio "property Item overlayParent"   "AudioMenu must accept an overlayParent (chrome host)."
Assert-Contains $audio "menu.mapToItem(overlayParent"  "AudioMenu must position itself in overlay coordinates."
Assert-Contains $player "overlayParent: chrome"        "PlayerPage must host the subtitle/audio popovers on the chrome layer."

# ---- 3. SHAPE: the premature close is gone — a pick waits for mpv, it does not force-close on emit ----
if ($sub -match "trackPicked\(String\(modelData\.id\)\);\s*\r?\n\s*menu\.panelOpen = false") {
    throw "Subtitle rows must NOT close on emit - they must wait for mpv to confirm (pickTrack + resolvePending)."
}
Assert-Contains $sub "function pickTrack"   "SubtitleMenu must route row picks through the confirm-before-close pickTrack()."
Assert-Contains $sub "function failPending" "SubtitleMenu must surface a visible error when a selection fails or times out."

Write-Host "Player subtitle click-through + confirm-before-close checks passed."
