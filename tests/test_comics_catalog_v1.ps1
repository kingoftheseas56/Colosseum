$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw -LiteralPath (Join-Path $root $relativePath)
}

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (!$text.Contains($needle)) { throw $message }
}

function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { throw $message }
}

$main = Read-RepoFile "qml/Main.qml"
$world = Read-RepoFile "qml/TankobanWorld.qml"
$ledger = Read-RepoFile "qml/ComicDbLedger.qml"
$loader = Read-RepoFile "qml/ComicsDbLoader.qml"

# P4 (2026-07-18): the curated catalog moved into comics_catalog.db, read through the
# ComicsCatalog engine — comics_db.gen.js is RETIRED. The old "world owns the lazy gen.js
# import" needles consciously flip: nothing may import the generated file anymore, and both
# ingest points (world bootstrap + shell-side loader) must ride ComicsDb.setEngine instead.
Assert-NotContains $main 'comics_db.gen.js' `
    "Main.qml must not touch the retired generated catalog."
Assert-NotContains $world 'comics_db.gen.js' `
    "TankobanWorld.qml must not import the retired generated catalog."
Assert-NotContains $loader 'comics_db.gen.js' `
    "ComicsDbLoader.qml must not import the retired generated catalog."
Assert-Contains $world 'ComicsDb.setEngine' `
    "TankobanWorld.qml must hand the catalogue engine to ComicsDb when its Loader creates the world."
Assert-Contains $loader 'ComicsDb.setEngine' `
    "ComicsDbLoader.qml must hand the catalogue engine to ComicsDb for shell-side routing."
Assert-Contains $ledger 'property bool   hasSource: !!ed.modelData.available && postUrl.length > 0' `
    "Ledger availability must require a verified GetComics post."
Assert-Contains $ledger 'if (typeof Comics === "undefined" || !chId.length || !canAcquire) return' `
    "The ledger primary action must reject unavailable editions."
Assert-NotContains $ledger 'downloadIssueTorrent' `
    "The ledger must never auto-pick a torrent source."
Assert-Contains $ledger 'ed.modelData.display_title || ed.modelData.title' `
    "Ledger rows and download labels must prefer the exact-ISBN canonical edition name."

# The JS logic harness (comics_catalog_logic_harness.qml) retired 2026-07-18 with the
# pre-reboot Top-Comics ranked wall and its catalog-model JS. The catalogue engine's own
# ranked/genre/downloadable logic is covered C++-side by comics_catalog_engine_harness.cpp;
# what remains here are the retirement + setEngine + ledger-availability static contracts.

Write-Host "comics catalog v1: OK"
