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
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- Task 2: tab bar = 3 tabs, centered ---
$tabbar = Read-File "qml/TheatreTabBar.qml"
Assert-NotContains $tabbar 'key: "discover"' "Discover tab must be removed from tabModel."
Assert-Contains $tabbar 'anchors.horizontalCenter: parent.horizontalCenter' "Tab bar Glass must be centered."
Assert-Contains $tabbar '/ 3' "Pill width must divide by 3 tabs."

# --- Task 3: TheatreApi reduced to Top 10 specs ---
$api = Read-File "qml/TheatreApi.js"
Assert-Contains $api '"Top 10 on Movies"' "Movies page must be a single Top 10 row."
Assert-Contains $api '"Top 10 on Shows"' "Shows page must be a single Top 10 row."
Assert-Contains $api '"Top 10 on Anime"' "Anime Top Airing row must be retitled Top 10 on Anime."
Assert-NotContains $api 'function discoverSpecs' "discoverSpecs must be deleted."
Assert-NotContains $api '"Top Drama"' "Per-genre rows must be gone from page specs."

# --- Task 4: catalog page = Top 10 + GenreMosaic, no heroes ---
$page = Read-File "qml/TheatreCatalogPage.qml"
Assert-Contains $page 'GenreMosaic' "Catalog page must render the genre boxes."
Assert-Contains $page 'signal genreRequested(string kind, string name)' "Catalog page must emit genreRequested."
Assert-Contains $page 'signal genreIndexRequested(string kind)' "Catalog page must emit genreIndexRequested."
Assert-NotContains $page 'TheatreCinemaHero' "Per-tab heroes must be removed."
Assert-NotContains $page 'TheatrePeekHero' "Per-tab heroes must be removed."

# --- Task 5: world defaults + signal forwarding ---
$world = Read-File "qml/TheatreWorld.qml"
Assert-Contains $world 'activeTab: "movies"' "Default tab must be movies."
Assert-Contains $world 'signal theatreGenreRequested(string kind, string name)' "World must forward genre opens."
Assert-Contains $world 'signal theatreGenreIndexRequested(string kind)' "World must forward index opens."

# --- Task 6: Theatre genre API exists with the right surface ---
$gapi = Read-File "qml/TheatreGenreApi.js"
Assert-Contains $gapi 'function loadGenre(kind, name, sort, push)' "TheatreGenreApi.loadGenre(kind,...) required."
Assert-Contains $gapi 'function loadGroups(kind' "TheatreGenreApi.loadGroups(kind,...) required."
Assert-Contains $gapi 'function siblings(kind)' "TheatreGenreApi.siblings(kind) required."
Assert-Contains $gapi '/genres/anime' "Anime genre ids/counts must come live from Jikan."
Assert-Contains $gapi 'cinemeta-catalogs.strem.io' "Movies/Shows genre data must come from Cinemeta."
Assert-NotContains $gapi 'themoviedb' "No TMDB. Ever."

# --- Task 7/8: the two cloned pages ---
$gpage = Read-File "qml/TheatreGenrePage.qml"
Assert-Contains $gpage 'property string mediaKind' "Genre page must carry mediaKind."
Assert-Contains $gpage 'signal itemRequested(var item)' "Genre page must emit itemRequested(item)."
Assert-Contains $gpage 'TheatreGenreApi.js' "Genre page must use the Theatre API clone."
$gindex = Read-File "qml/TheatreGenreIndex.qml"
Assert-Contains $gindex 'property string mediaKind' "Genre index must carry mediaKind."
Assert-Contains $gindex 'TheatreGenreApi.js' "Genre index must use the Theatre API clone."

# --- Task 9: Main wiring ---
$main = Read-File "qml/Main.qml"
Assert-Contains $main 'theatreGenreLayer' "Main must host the Theatre genre layer."
Assert-Contains $main 'theatreGenreIndexLayer' "Main must host the Theatre genre index layer."
Assert-Contains $main 'function openTheatreGenre(' "Main must expose openTheatreGenre."
Assert-Contains $main 'function openTheatreGenreIndex(' "Main must expose openTheatreGenreIndex."
Assert-Contains $main 'theatreGenreLayer.active) win.closeTheatreGenre()' "Esc chain must close the genre page."

# --- lane discipline: manga machinery untouched ---
foreach ($f in @("qml/GenrePage.qml", "qml/GenreIndex.qml", "qml/GenreApi.js", "qml/GenreIndexApi.js")) {
    $t = Read-File $f
    Assert-NotContains $t 'mediaKind' "LANE VIOLATION: $f must not be parameterized."
}

Write-Host "Theatre Top10 + genre boxes contract checks passed."
