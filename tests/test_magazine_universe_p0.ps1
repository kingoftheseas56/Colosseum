$ErrorActionPreference = "Stop"

# Magazine universe contract — THE LONG RUN form (A5, 2026-07-18, fresh design on Hemanth's
# order; the prior archive concept is dead). Weekly Shonen Jump publishes MANGA — the page
# carries no watch machinery. The page speaks print velocity: the press-block hero, the run
# chart (serializations drawn to scale from real years), Running Now (the live publishing
# registry), Most Collected (MAL members, NEVER "circulation"), and Every Series (the full
# registry wall, filed progressively with honest partial states). MAL magazine 83 is the
# registry spine; AniList carries the art via verified flagship id-pins in Universes.js.
# Shape contract only — behavior lives in magazine_registry_harness.qml and
# magazine_page_load_harness.qml.

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Lacks($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

$mp = Read-File "qml/MagazineUniversePage.qml"
foreach ($n in @('signal seriesRequested(string title)', 'import QtQuick.Controls',
                 'import "MagazineApi.js" as Mag',
                 'THE LONG RUN', 'RUNNING NOW', 'MOST COLLECTED', 'EVERY SERIES',
                 'MAL members', 'Mag.buildRuns', 'Mag.loadSummary', 'Mag.fetchArchivePage',
                 'Mag.mergeDedup', 'Mag.mapFlagship', 'Mag.mapLineup', 'root.flagships',
                 'currentLineup',
                 'activeFocusOnTab: true', 'Keys.onReturnPressed')) {
    Assert-Contains $mp $n "MagazineUniversePage must carry: $n"
}
Assert-Lacks $mp 'watchRequested' "The magazine has no watch verb (manga only, ratified)."
Assert-Lacks $mp 'Cinemeta' "The magazine touches no watch source."
Assert-Lacks $mp 'movieQueries' "The magazine fires no film queries."
# THE HONESTY LAW: MAL members are library counts — the word "circulation" is banned from
# the whole lane so the mislabel can never return.
Assert-Lacks $mp 'circulation' "MAL members must never be labeled circulation."
# the dead concept stays dead
Assert-Lacks $mp 'Editorial Archive' "The archive concept is retired."
Assert-Lacks $mp 'ArchiveVolume' "The archive-volume machinery is retired."
Assert-Lacks $mp 'Current Desk' "The desk metaphor is retired."
# offline honesty: short factual notices, no invention
Assert-Contains $mp "isn't responding right now" "Offline states must say the factual truth."

$mag = Read-File "qml/MagazineApi.js"
foreach ($n in @('function loadSummary(', 'function fetchArchivePage(', 'function hasPage(',
                 'function buildRuns(', 'function sortBy(', 'function decadeOf(',
                 'function mergeDedup(', 'function mapEntry(', 'function mapFlagship(',
                 'function mapLineup(', 'magazines=', 'order_by=members')) {
    Assert-Contains $mag $n "MagazineApi must carry: $n"
}
Assert-Lacks $mag 'circulation' "The registry lane must never speak of circulation."

$udb = Read-File "qml/Universes.js"
Assert-Contains $udb 'category: "magazine"' "Weekly Shonen Jump must ride the magazine template."
Assert-Contains $udb 'malMagazineId: 83' "Jump must pin its MAL magazine registry id."
Assert-Contains $udb 'flagships:' "Jump must curate the AniList-pinned flagships."
Assert-Contains $udb 'currentLineup:' "Jump must curate the Wikipedia current-series lineup."
Assert-Contains $udb 'al: 150440' "RuriDragon must ride the 2022 serial pin, not the one-shot."
Assert-Contains $udb 'al: 116827' "Burn the Witch must ride the 2020 season pin, not the pilot."
Assert-Contains $udb 'al: 30013' "One Piece must ride its verified AniList pin."
Assert-Contains $udb 's4.anilist.co' "Flagship covers must ride the pinned AniList CDN."
Assert-Contains $udb 'milestones:' "Jump must carry its verified print milestones."
Assert-Lacks $udb 'fallbackEras' "The era-fallback machinery is retired with the archive."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'MagazineUniversePage.qml' "Main must route magazine universes to the magazine template."

Write-Host "magazine universe p0: OK"
