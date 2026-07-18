$ErrorActionPreference = "Stop"

# Magazine universe contract — THE EDITORIAL ARCHIVE form (A5, 2026-07-18 free-reign
# commission; spec: docs/superpowers/specs/2026-07-16-weekly-shonen-jump-editorial-archive-
# design.md). Weekly Shonen Jump publishes MANGA — the page carries no watch machinery.
# The page represents the ARCHIVE, not this week's issue: masthead with the four bound era
# volumes, The Current Desk (live serialization registry, honestly labeled), the Hall of
# Champions (most collected on MAL — member counts, NEVER "circulation"), the four archive
# volumes on ivory paper, and the progressive complete registry index with honest
# partial/offline states. Shape contract only — behavior lives in the headless harness
# (magazine_registry_harness.qml).

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
                 'THE CURRENT DESK', 'THE HALL OF CHAMPIONS', 'THE ARCHIVE', 'THE COMPLETE REGISTRY',
                 'THE PRINT RECORD', 'Most collected on MAL', 'MAL members',
                 'Mag.loadSummary', 'Mag.fetchArchivePage', 'Mag.bucketByEra', 'Mag.mergeDedup',
                 'RESUME FILING', 'fallbackFor', 'scrollToVolume',
                 'activeFocusOnTab: true', 'Keys.onReturnPressed')) {
    Assert-Contains $mp $n "MagazineUniversePage must carry: $n"
}
Assert-Lacks $mp 'watchRequested' "The magazine has no watch verb (manga only, ratified)."
Assert-Lacks $mp 'Cinemeta' "The magazine touches no watch source."
Assert-Lacks $mp 'movieQueries' "The magazine fires no film queries."
# THE HONESTY LAW (spec 2): MAL members are library counts — the word "circulation" is
# banned from the whole lane so the mislabel can never return.
Assert-Lacks $mp 'circulation' "MAL members must never be labeled circulation."
# offline honesty: the desk never invents, the champions fall back unranked
Assert-Contains $mp 'Current registry unavailable' "The desk must state its offline truth."
Assert-Contains $mp 'the curated lineup stands in' "Champions must fall back to curation, unranked."

$mag = Read-File "qml/MagazineApi.js"
foreach ($n in @('function loadSummary(', 'function fetchArchivePage(', 'function hasPage(',
                 'function bucketByEra(', 'function undatedOf(', 'function sortEra(',
                 'function alphaSort(', 'function mergeDedup(', 'function mapEntry(',
                 'magazines=', 'order_by=members', 'status=publishing',
                 'The Founding Years', 'The Golden Age', 'The Big Three Era', 'The New Generation')) {
    Assert-Contains $mag $n "MagazineApi must carry: $n"
}
Assert-Lacks $mag 'circulation' "The registry lane must never speak of circulation."
# the approved era boundaries, exact (spec 1)
foreach ($n in @('from: 1968, to: 1979', 'from: 1980, to: 1996',
                 'from: 1997, to: 2014', 'from: 2015, to: 9999')) {
    Assert-Contains $mag $n "The archive must keep the approved era boundary: $n"
}

$udb = Read-File "qml/Universes.js"
Assert-Contains $udb 'category: "magazine"' "Weekly Shonen Jump must ride the magazine template."
Assert-Contains $udb 'malMagazineId: 83' "Jump must pin its MAL magazine registry id."
Assert-Contains $udb 'fallbackEras:' "Jump must curate era fallback flagships for the offline volumes."
Assert-Contains $udb 'heroLine:' "Jump must carry its sourced hero line."
Assert-Contains $udb 'eraNotes:' "Jump must carry sourced per-volume historical notes."
Assert-Contains $udb 'milestones:' "Jump must carry its verified print milestones."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'MagazineUniversePage.qml' "Main must route magazine universes to the magazine template."

Write-Host "magazine universe p0: OK"
