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

# --- Tab bar: the three deep-catalogue tabs exist and the bar is centered. The current
#     Theatre build carries Discover + Library alongside Movies/Shows/Anime; the deep
#     catalogue only owns the Movies/Shows/Anime bodies, so we assert those three tabs are
#     present and the bar divides its width by the actual tab count (superseding the retired
#     3-tab / Discover-removed contract). ---
$tabbar = Read-File "qml/TheatreTabBar.qml"
Assert-Contains $tabbar 'key: "movies"' "Movies tab must exist in the Theatre tab bar."
Assert-Contains $tabbar 'key: "shows"' "Shows tab must exist in the Theatre tab bar."
Assert-Contains $tabbar 'key: "anime"' "Anime tab must exist in the Theatre tab bar."
Assert-Contains $tabbar 'anchors.horizontalCenter: parent.horizontalCenter' "Tab bar Glass must be centered."
Assert-Contains $tabbar 'tabs.tabModel.length' "Pill width must divide by the actual tab count."

# --- Deep catalogue (2026-08-01): tabs are no longer a single Top 10 row. Top 10 stays the
#     FIRST shelf on every tab and genres stay LAST; those ordering guarantees, plus no-award
#     and no-blurb, are proven behaviourally by tests/theatre_catalog_rules_harness.qml. Here
#     we retain only the static guarantees the deep catalogue must still honour. ---
$rules = Read-File "qml/TheatreCatalogRules.js"
Assert-Contains $rules 'top-10' "Rules must define the Top 10 shelf key on every tab."
Assert-Contains $rules '"Top 10"' "Rules must keep the ranked shelf titled 'Top 10'."

$api = Read-File "qml/TheatreApi.js"
Assert-NotContains $api 'themoviedb' "No TMDB. Ever."
Assert-NotContains $api 'api_key' "Theatre catalogue must stay keyless (no API keys)."

# --- Task 4: catalog page = Top 10 + GenreMosaic, no heroes ---
$page = Read-File "qml/TheatreCatalogPage.qml"
Assert-Contains $page 'GenreMosaic' "Catalog page must render the genre boxes."
Assert-Contains $page 'signal genreRequested(string kind, string name)' "Catalog page must emit genreRequested."
Assert-Contains $page 'signal genreIndexRequested(string kind)' "Catalog page must emit genreIndexRequested."
Assert-NotContains $page 'TheatreCinemaHero' "Per-tab heroes must be removed."
Assert-NotContains $page 'TheatrePeekHero' "Per-tab heroes must be removed."

# --- World: routes the catalogue tabs and forwards genre opens. (The default landing tab is
#     Discover in the current build; the deep catalogue owns the movies/shows/anime bodies, so
#     we assert the movies tab routes to the catalogue page rather than pinning the default.) ---
$world = Read-File "qml/TheatreWorld.qml"
Assert-Contains $world 'property string activeTab' "World must own the active-tab state."
Assert-Contains $world 'activeTab === "movies"' "World must route the movies tab to the catalogue page."
Assert-Contains $world 'signal theatreGenreRequested(string kind, string name)' "World must forward genre opens."
Assert-Contains $world 'signal theatreGenreIndexRequested(string kind)' "World must forward index opens."

# --- Task 6: Theatre genre API exists with the right surface ---
$gapi = Read-File "qml/TheatreGenreApi.js"
Assert-Contains $gapi 'function loadGenre(kind' "TheatreGenreApi.loadGenre(kind,...) required."
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
