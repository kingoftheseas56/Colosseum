$ErrorActionPreference = "Stop"

# Theatre wiring contract for the keyless anime ordering feature (spec 2026-07-15).
# TheatreSeries.qml must consume the native AnimeOrder service + presentation
# module, keep source + canonical models side by side, expose the Absolute/Seasons
# selector only through the completeness flag, and preserve provider identities.
# It must NEVER touch the datasets directly or make a QML network request.

$root = Split-Path -Parent $PSScriptRoot
$series = Get-Content -Raw -LiteralPath (Join-Path $root "qml\TheatreSeries.qml")

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch [regex]::Escape($Pattern)) { throw "MISSING: $Message" }
}
function Assert-Absent($Text, $Pattern, $Message) {
    if ($Text -match [regex]::Escape($Pattern)) { throw "FORBIDDEN: $Message" }
}

Assert-Contains $series 'import "AnimeEpisodePresentation.js" as AnimeEpisodePresentation' `
    "TheatreSeries must import the presentation module."
Assert-Contains $series 'property var sourceVideos' "TheatreSeries must keep the raw provider list separate."
Assert-Contains $series 'property var animeOrder' "TheatreSeries must hold the canonical order model."
Assert-Contains $series 'property string episodeOrderMode' "TheatreSeries must track the requested order mode."
Assert-Contains $series 'AnimeOrder.resolve' "TheatreSeries must resolve through the native service."
Assert-Contains $series 'AnimeOrder.revision' "TheatreSeries must recompute on a new service revision."
Assert-Contains $series 'Absolute' "TheatreSeries must offer an Absolute view."
Assert-Contains $series 'Seasons' "TheatreSeries must offer a Seasons view."
Assert-Contains $series 'absoluteComplete' "TheatreSeries must gate Absolute on a complete mapping."
Assert-Contains $series 'sourceSeason' "TheatreSeries accessors must prefer the canonical source season."
Assert-Contains $series 'sourceEpisode' "TheatreSeries accessors must prefer the canonical source episode."
Assert-Contains $series 'streamId' "TheatreSeries must key on the preserved provider stream id."
Assert-Contains $series 'kind === "special"' "TheatreSeries must badge specials honestly."

Assert-Absent $series 'thexem' "TheatreSeries must not reference XEM."
Assert-Absent $series 'anime-list-master.xml' "TheatreSeries must not fetch the mapping dataset from QML."
Assert-Absent $series 'anime-list-mini.json' "TheatreSeries must not fetch the identity dataset from QML."
Assert-Absent $series 'XMLHttpRequest' "TheatreSeries must not make a QML network request for ordering."

Write-Host "Theatre anime ordering contract OK."
