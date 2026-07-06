$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Only the destructive path asks: ending a session mid-playback. Minimize stays instant
# (spec 2026-07-06 slice 5).
Assert-Contains $player 'property bool closeConfirmOpen' `
    "Close-confirm state must exist."
Assert-Contains $player 'End this session?' `
    "The confirm must ask plainly."
Assert-Contains $player 'Your spot stays in Continue Watching.' `
    "The confirm must state the safety net."
Assert-Contains $player 'if (playing) { root.closeConfirmOpen = true' `
    "Close must only ask while actively playing."

Write-Host "Player close-confirm contract checks passed."
