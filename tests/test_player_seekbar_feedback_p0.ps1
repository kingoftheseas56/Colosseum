$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Buffered fill: the bar shows how far ahead is loaded (spec 2026-07-06 slice 4).
Assert-Contains $player 'mpv.cacheTime' `
    "Seek bar must render a buffered fill from demuxer cache time."
# Pending seek: the bar answers a jump immediately, not when the stream catches up.
Assert-Contains $player 'property real seekTargetSec' `
    "A pending seek target must exist."
Assert-Contains $player 'seekSettling' `
    "A settling state must drive the dot/time while a seek is in flight."
Assert-Contains $player 'seekSettleGuard' `
    "A guard timer must clear a target mpv never acknowledges."
Assert-Contains $player 'root.fmtTime(root.displayPosition())' `
    "The elapsed-time readout must consume displayPosition (wiring, not just machinery)."

Write-Host "Player seek bar feedback contract checks passed."
