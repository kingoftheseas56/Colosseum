$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Prev/Next episode must ride the SAME stream pipeline as the main picker —
# every installed extension, Torrentio only as fallback (spec 2026-07-06 slice 6).
Assert-Contains $player 'import "AddonClient.js" as AddonClient' `
    "PlayerPage must import the all-extensions stream client."
Assert-Contains $player 'AddonClient.streamExtensions(Extensions.installed()' `
    "Adjacent episodes must gather installed stream extensions."
Assert-Contains $player 'AddonClient.loadStreams(exts' `
    "Adjacent episodes must ask all extensions in parallel."

Write-Host "Player adjacent-extensions contract checks passed."
