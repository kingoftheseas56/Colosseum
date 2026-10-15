$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$mpvh = Get-Content (Join-Path $root "native/player/mpvitem.h") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

Assert-Contains $mpvh 'Q_INVOKABLE void frameStep()' `
    "MpvItem must expose frameStep()."
Assert-Contains $mpvh 'Q_INVOKABLE void frameBackStep()' `
    "MpvItem must expose frameBackStep()."
# Paused: , . step frames. Playing: they keep the 30s skips (spec 2026-07-06 slice 5).
Assert-Contains $player 'if (mpv.pause) mpv.frameBackStep(); else root.seekStep(-30)' `
    "Comma must frame-step when paused, 30s-skip when playing."
Assert-Contains $player 'if (mpv.pause) mpv.frameStep(); else root.seekStep(30)' `
    "Period must frame-step when paused, 30s-skip when playing."

Write-Host "Player frame-step contract checks passed."
