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
# Year rides EVERY playback context: episode-queue return, movie play, and both
# inline adjacent contexts. Pinned at the real count (4) so a dropped site fails.
$yearHands = [regex]::Matches($series, '"year": page\.year').Count
if ($yearHands -lt 4) { throw "TheatreSeries must hand year on every playback context (episode queue, movie play, both inline adjacents); found $yearHands." }

# General sweep: NO direct string writes to mediaSubtitle anywhere - every transport
# label must route through mediaTransport + updateMediaSubtitle(). (The legitimate
# assignment inside updateMediaSubtitle assigns from parts.join, not a string literal.)
$directWrites = [regex]::Matches($player, 'root\.mediaSubtitle\s*=\s*"').Count
if ($directWrites -ne 0) { throw "mediaSubtitle must only be written by updateMediaSubtitle() (found $directWrites direct string writes)." }

# Stronger sweep: mediaSubtitle is assigned EXACTLY once in the whole file - the
# parts.join line inside updateMediaSubtitle. Catches strays whose RHS is an
# expression rather than a string literal (the Live channel.group leak was one).
$allWrites = [regex]::Matches($player, 'root\.mediaSubtitle\s*=').Count
if ($allWrites -ne 1) { throw "mediaSubtitle must have exactly one assignment (updateMediaSubtitle's parts.join); found $allWrites." }

Write-Host "Player title-line contract checks passed."
