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

$world = Read-File "qml/BiblioWorld.qml"
Assert-Contains $world 'signal biblioGenreIndexRequested()' "World must forward the Explore corner."
Assert-Contains $world 'onExploreClicked: biblio.biblioGenreIndexRequested()' "GenreMosaic Explore corner must be wired."

$main = Read-File "qml/Main.qml"
Assert-Contains $main 'biblioGenreIndexLayer' "Main must host the Biblio genre index layer."
Assert-Contains $main 'function openBiblioGenreIndex(' "Main must expose openBiblioGenreIndex."
Assert-Contains $main 'biblioGenreIndexLayer.active) win.closeBiblioGenreIndex()' "Esc chain must close the index."

Write-Host "Biblio explore index contract checks passed."
