$ErrorActionPreference = "Stop"

# Player hydration contract for keyless anime ordering (spec 2026-07-15).
# Bare-door hydration (Continue Watching / downloaded episode) must resolve the
# canonical order through the native service, prefer the absolute queue, fall
# back to the existing same-season queue, reject stale callbacks by hydrateGen
# and identity, retry on a new service revision, and never touch the datasets.

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content -Raw -LiteralPath (Join-Path $root "qml\PlayerPage.qml")

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch [regex]::Escape($Pattern)) { throw "MISSING: $Message" }
}
function Assert-Absent($Text, $Pattern, $Message) {
    if ($Text -match [regex]::Escape($Pattern)) { throw "FORBIDDEN: $Message" }
}

Assert-Contains $player 'AnimeOrder.resolve' "Player must resolve the canonical order via the native service."
Assert-Contains $player 'queueContextFromOrder' "Player must build the canonical absolute queue first."
Assert-Contains $player 'queueContextFromMeta' "Player must keep the same-season fallback."
Assert-Contains $player 'AnimeOrder.revision' "Player must retry when a new generation installs."
Assert-Contains $player 'seriesRootId(root.mediaId)' "Player must compare the resolved identity before a callback lands."
Assert-Contains $player 'hydrateGen' "Player must reject stale hydration callbacks by generation."
Assert-Contains $player 'playbackQueueOrderingMode' "Player must record the accepted ordering mode."

Assert-Absent $player 'thexem' "Player must not reference XEM."
Assert-Absent $player 'anime-list-master.xml' "Player must not fetch the mapping dataset."
Assert-Absent $player 'anime-list-mini.json' "Player must not fetch the identity dataset."
Assert-Absent $player 'raw.githubusercontent' "Player must not fetch datasets directly."

Write-Host "Player anime ordering contract OK."
