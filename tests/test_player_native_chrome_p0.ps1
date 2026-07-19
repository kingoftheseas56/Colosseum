$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# Harbor-parity chrome (approved 2026-07-19): restrained top/bottom black scrims instead of the
# old fused Plasma glass panel; a NOW PLAYING micro-label; a single fullscreen control living in
# the bottom transport group (relocated out of the top window group); a thin 3 px timeline.
# docs/superpowers/specs/2026-07-19-colosseum-harbor-player-polish-design.md
Assert-Contains    $player 'id: playerTopScrim'         "Top edge readability must come from a restrained scrim gradient, not a fused bar."
Assert-Contains    $player 'id: playerBottomScrim'      "Bottom edge readability must come from a restrained scrim gradient, not a fused panel."
Assert-Contains    $player 'text: "NOW PLAYING"'        "The top-left group must carry the NOW PLAYING micro-label."
Assert-Contains    $player 'id: bottomFullscreenButton' "The single fullscreen control must live in the bottom transport group."
Assert-NotContains $player 'id: topFullscreenButton'    "There must be no second (top) fullscreen control."
Assert-Contains    $player 'height: 3'                  "The timeline track must be 3 px at rest."

Write-Host "Native chrome contract checks passed."
