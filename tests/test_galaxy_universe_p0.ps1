$ErrorActionPreference = "Stop"

# Galaxy universe template contract (Star Wars, 2026-07-12): the generic page starved empty
# (modern shows carry no "Star Wars" in their names) - so the galaxy is CURATED: trilogy
# triptych, standalones, live/animated rails, all slotted by canon. Watch = Episode IV.

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

$gp = Read-File "qml/GalaxyUniversePage.qml"
foreach ($n in @('The Skywalker Saga', 'loadGalaxy', 'Begin the saga', 'import QtQuick.Controls',
                 'Live Action', 'signal watchRequested(var item)')) {
    Assert-Contains $gp $n "GalaxyUniversePage must carry: $n"
}
Assert-Lacks $gp 'AniList' "The galaxy has no manga machinery."
Assert-Lacks $gp 'GridView' "The galaxy is a triptych + rails, not a grid."

$sa = Read-File "qml/SagaApi.js"
Assert-Contains $sa 'function loadGalaxy(' "SagaApi must carry the galaxy loader."

$udb = Read-File "qml/Universes.js"
Assert-Contains $udb 'category: "galaxy"' "Star Wars must ride the galaxy template."
Assert-Contains $udb 'The Prequels' "The trilogy canon must be curated."
Assert-Contains $udb 'firstWatch: "Star Wars: Episode IV - A New Hope"' "The golden path must be Episode IV."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'GalaxyUniversePage.qml' "Main must route galaxy universes to the galaxy template."

Write-Host "galaxy universe p0: OK"
