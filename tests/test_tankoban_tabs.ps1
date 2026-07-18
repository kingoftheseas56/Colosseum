$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}

# --- Task 1: WorldTabBar is a generic, parameterized glass tab bar ---
$wtb = Get-Content (Join-Path $root "qml/WorldTabBar.qml") -Raw
Assert-Contains $wtb 'property var tabModel' "tab bar takes its tabs as a model (not hardcoded)"
Assert-Contains $wtb 'signal tabRequested(string tab)' "tab bar emits the selected tab key"
Assert-Contains $wtb 'property string currentTab' "tab bar highlights the active tab"
Assert-Contains $wtb 'tabs.tabModel.length' "pill width divides by the tab count, not a hardcoded 3"
Assert-Absent  $wtb ') / 3' "no hardcoded 3-tab division survives the generalization"

# --- Task 2: TankobanMangaTab holds ONLY the manga browse rows + emits manga signals ---
$mtab = Get-Content (Join-Path $root "qml/TankobanMangaTab.qml") -Raw
Assert-Contains $mtab 'Top in Tankoban — Manga' "manga tab carries the top-manga row"
Assert-Contains $mtab 'Explore by Genre — Manga' "manga tab carries the manga genre mosaic"
Assert-Contains $mtab 'signal seriesRequested(string title)' "manga tab emits series open up to the world"
Assert-Contains $mtab 'signal genreRequested(string name)' "manga tab emits genre open"
Assert-Contains $mtab 'signal genreIndexRequested()' "manga tab emits the genre-index explore"
Assert-Absent  $mtab 'Top in Tankoban — Comics' "manga tab must not carry any comics row"

# --- Task 3: TankobanComicsTab holds ALL comics browse rows, takes data as props, emits comics signals ---
$ctab = Get-Content (Join-Path $root "qml/TankobanComicsTab.qml") -Raw
Assert-Contains $ctab 'Top in Tankoban — Comics' "comics tab carries the top-comics row"
Assert-Contains $ctab 'model: comicsTab.comicShelves' "comics tab carries the shelf-row repeater"
Assert-Contains $ctab 'Explore Comics' "comics tab carries the explore-comics mosaic"
Assert-Contains $ctab 'property var comicRows' "comics tab receives its data as props (no re-fetch on switch)"
Assert-Contains $ctab 'property var comicShelves' "shelves passed in"
Assert-Contains $ctab 'property var comicBoxes' "explore boxes passed in"
Assert-Contains $ctab 'property var comicCovers' "explore covers passed in"
Assert-Contains $ctab 'signal gcdSeriesRequested(var d)' "shelf tiles open the run page"
Assert-Contains $ctab 'signal comicSeriesRequested(var d)' "top-comics tile opens the series"
Assert-Contains $ctab 'signal westernRequested(string title)' "top-comics fallback path"
Assert-Contains $ctab 'signal westernExploreRequested(var box)' "explore box opens the archive index"
Assert-Absent  $ctab 'Top in Tankoban — Manga' "comics tab must not carry any manga row"

Write-Host "tankoban tabs contract OK"
