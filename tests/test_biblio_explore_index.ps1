$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

$idx = Read-File "qml/BiblioGenreIndex.qml"
Assert-Contains $idx 'Catalog.biblioGenres' "Index must feed from the baked Catalog.biblioGenres (zero network)."
Assert-Contains $idx 'signal genrePicked(string name)' "Index must emit genrePicked."

$page = Read-File "qml/BiblioGenrePage.qml"
Assert-Contains $page 'signal exploreRequested()' "Genre page must emit exploreRequested."
Assert-Contains $page 'onClicked: root.exploreRequested()' "The Explore pill must be live, not decorative."

# BiblioWorld.qml's forwarding of the old genre-mosaic Explore corner (`signal
# biblioGenreIndexRequested()` / `onExploreClicked: biblio.biblioGenreIndexRequested()`) was
# INTENTIONALLY retired by the Discover/Explore split plan's Task 8 (2026-08-01-biblio-discover-
# explore.md, "retire old genre-route connections from BiblioWorld") — BiblioWorld.qml's own
# header comment now documents "GenreMosaic/BiblioGenrePage/BiblioGenreIndex are RETIRED from
# this world". This is not a regression to guard against; asserting it would only pin a
# deliberately-removed wire. BiblioGenreIndex.qml/BiblioGenrePage.qml/Main.qml's host layer stay
# guarded below because Task 8 explicitly left those legacy files in place elsewhere for
# compatibility until a later cleanup.

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'biblioGenreIndexLayer' "Main must host the Biblio genre index layer."
Assert-Contains $main 'function openBiblioGenreIndex(' "Main must expose openBiblioGenreIndex."
Assert-Contains $main 'biblioGenreIndexLayer.active) win.closeBiblioGenreIndex()' "Esc chain must close the index."

Write-Host "Biblio explore index contract checks passed."
