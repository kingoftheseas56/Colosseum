$ErrorActionPreference = "Stop"

# Magazine universe template contract (Hemanth 2026-07-12): Weekly Shonen Jump publishes
# MANGA — the page carries no watch machinery at all, and the lineup ranks by the curated
# query order (the reader's vote), never by network arrival.
# Second form (Hemanth, same day: "more custom and unique than a top-10"): the page rides
# MAL's serialization REGISTRY via Jikan (MagazineApi) — In This Issue (currently running),
# the All-Time Vote (ranked by real MAL members), Back Issues (start-year era shelves) —
# and the curated ten stay as the fallback so the room never stands empty offline.

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
foreach ($n in @('signal seriesRequested(string title)', 'loadMangaOnly', 'This Week', 'import QtQuick.Controls',
                 'import "MagazineApi.js" as Mag', 'In This Issue', 'The All-Time Vote', 'Back Issues',
                 'Mag.loadRegistry', 'Mag.bucketByEra')) {
    Assert-Contains $mp $n "MagazineUniversePage must carry: $n"
}
Assert-Lacks $mp 'watchRequested' "The magazine has no watch verb (manga only, ratified)."
Assert-Lacks $mp 'Cinemeta' "The magazine touches no watch source."
Assert-Lacks $mp 'movieQueries' "The magazine fires no film queries."
# the fallback law: the curated roster must still stand when the registry is unreachable
Assert-Contains $mp 'root.allTime.length > 0 ? root.allTime : root.uni.manga' "The vote must fall back to the curated ten offline."

$api = Read-File "qml/UniverseApi.js"
Assert-Contains $api 'function loadMangaOnly(' "UniverseApi must carry the magazine's manga-only loader."
# NOTE: -like treats [] as a char class — needle avoids the index brackets
Assert-Contains $api 'var slots = new Array(readQueries.length)' "The lineup must rank by curated query order, not arrival."

$mag = Read-File "qml/MagazineApi.js"
foreach ($n in @('function loadRegistry(', 'function bucketByEra(', 'function mapEntry(',
                 'magazines=', 'order_by=members', 'status=publishing')) {
    Assert-Contains $mag $n "MagazineApi must carry: $n"
}

$udb = Read-File "qml/Universes.js"
Assert-Contains $udb 'category: "magazine"' "Weekly Shonen Jump must ride the magazine template."
Assert-Contains $udb 'malMagazineId: 83' "Jump must pin its MAL magazine registry id."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'MagazineUniversePage.qml' "Main must route magazine universes to the magazine template."

Write-Host "magazine universe p0: OK"
