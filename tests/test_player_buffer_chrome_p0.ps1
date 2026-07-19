$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

# While a stream is LOADING (starting), the transport chrome steps aside so the full-bleed
# per-show cinematic loader owns the screen; the loader exits on truthful first-frame advance
# (or when the resume-choice prompt must become interactive), never on a fake percentage.
# docs/superpowers/specs/2026-07-19-colosseum-harbor-player-polish-design.md
Assert-Contains $player "root.controlsShown && !root.starting" `
    "Chrome must hide while a stream is loading (starting) so the loader owns the screen."
Assert-Contains $player 'root.loadingStatusText()' `
    "The loader must show a stable status line (Preparing stream / Buffering)."
Assert-Contains $player 'function finishStartingIfPlaybackAdvanced()' `
    "PlayerPage must clear the loading state when playback position advances, not only on file-loaded."
Assert-Contains $player 'onPositionChanged: root.finishStartingIfPlaybackAdvanced()' `
    "mpv position advance must dismiss the loader once playback has actually begun."

# The per-show cinematic loader (Task 5) replaces the old title-card blanker.
Assert-Contains $player 'PlayerLoadingScreen {' `
    "The per-show cinematic loader component must front the loading state."
Assert-Matches  $player 'active:\s*\(root\.starting\s*&&\s*!root\.resumeChoiceOpen\)\s*\|\|\s*root\.errored' `
    "The loader stays up for starting/errored but yields when the resume-choice prompt must become interactive."
Assert-Contains $player 'root.mediaLogo' `
    "The loader must be fed the per-show identity (logo/still) threaded through playTorrent."

# Guard: the loader must never suppress the buffering/starting line while a stream is still opening.
if ($player -like '*visible: text.length > 0 && (root.errored || root.statusMsg !== "Buffering...")*') {
    throw "Loading card must not suppress the buffering/starting line while a stream is still opening."
}

Write-Host "Player buffer-chrome contract checks passed."
