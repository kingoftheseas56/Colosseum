$ErrorActionPreference = "Stop"

# Magazine universe template contract (Hemanth 2026-07-12): Weekly Shonen Jump publishes
# MANGA — the page carries no watch machinery at all, and the lineup ranks by the curated
# query order (the reader's vote), never by network arrival.

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
foreach ($n in @('signal seriesRequested(string title)', 'loadMangaOnly', 'This Week', 'import QtQuick.Controls')) {
    Assert-Contains $mp $n "MagazineUniversePage must carry: $n"
}
Assert-Lacks $mp 'watchRequested' "The magazine has no watch verb (manga only, ratified)."
Assert-Lacks $mp 'Cinemeta' "The magazine touches no watch source."
Assert-Lacks $mp 'movieQueries' "The magazine fires no film queries."

$api = Read-File "qml/UniverseApi.js"
Assert-Contains $api 'function loadMangaOnly(' "UniverseApi must carry the magazine's manga-only loader."
# NOTE: -like treats [] as a char class — needle avoids the index brackets
Assert-Contains $api 'var slots = new Array(readQueries.length)' "The lineup must rank by curated query order, not arrival."

$udb = Read-File "qml/Universes.js"
Assert-Contains $udb 'category: "magazine"' "Weekly Shonen Jump must ride the magazine template."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'MagazineUniversePage.qml' "Main must route magazine universes to the magazine template."

Write-Host "magazine universe p0: OK"
