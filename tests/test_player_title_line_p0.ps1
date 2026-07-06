$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$series = Get-Content (Join-Path $root "qml/TheatreSeries.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# The second title line is built from metadata (S/E - year - quality - source),
# never a bare transport word (spec 2026-07-06 slice 2).
Assert-Contains $player 'function updateMediaSubtitle' `
    "PlayerPage must build the subtitle line from metadata."
Assert-NotContains $player 'root.mediaSubtitle = "Torrent stream"' `
    "Transport must be set via mediaTransport, not written to mediaSubtitle directly."
Assert-NotContains $player 'root.mediaSubtitle = "Downloaded"' `
    "Downloaded playback must route through updateMediaSubtitle too."
Assert-Contains $player 'root.mediaTransport = "Torrent stream"' `
    "Torrent transport must still be recorded (as the fallback tail)."
Assert-Contains $series '"year": page.year' `
    "TheatreSeries must hand the year to the player's playback context."

Write-Host "Player title-line contract checks passed."
