$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Arrow/dock skip size is a persisted choice, not a hardcode (spec 2026-07-06 slice 5).
Assert-Contains $player 'property int seekStepSeconds: 10' `
    "Persisted seekStepSeconds setting (default 10 = today's behavior) must exist."
Assert-Contains $player 'seekBackSeconds: playerSettings.seekStepSeconds' `
    "Back skip must ride the setting."
Assert-Contains $player 'seekForwardSeconds: playerSettings.seekStepSeconds' `
    "Forward skip must ride the setting."
Assert-Contains $player 'playerSettings.seekStepSeconds = modelData' `
    "The speed menu must offer the 5/10/30/60 choice."

Write-Host "Player seek-interval contract checks passed."
