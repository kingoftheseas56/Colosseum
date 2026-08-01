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

# --- Task 4 + Task 7: TankobanWorld keeps shared rows + comics data, mounts the tab bar,
#     and the browse rows live in the tab components. Task 7 adds Discover as the FIRST
#     and DEFAULT tab (Discover · Manga · Comics) with a retained TankobanDiscoverPage. ---
$tw = Get-Content (Join-Path $root "qml/TankobanWorld.qml") -Raw
Assert-Contains $tw 'property string activeTab: "discover"' "world holds the active tab, default discover (Task 7: Discover is first/default)"
Assert-Contains $tw 'WorldTabBar' "world mounts the tab bar"
Assert-Contains $tw '"discover"' "tab model has the discover key (Task 7)"
Assert-Contains $tw '"manga"' "tab model has the manga key"
Assert-Contains $tw '"comics"' "tab model has the comics key"
Assert-Contains $tw 'onTabRequested' "tab bar drives activeTab"
Assert-Contains $tw 'TankobanDiscoverPage' "world mounts the retained TankobanDiscoverPage (Task 7)"
Assert-Contains $tw 'TankobanComicsTab.qml' "world Loader-swaps to the comics half"
Assert-Contains $tw 'TankobanMangaTab.qml' "world Loader-swaps to the manga half"
Assert-Contains $tw 'Qt.binding' "comics data is reactively bound into the loaded view (no re-fetch)"
Assert-Contains $tw 'FeaturedCarousel' "featured stays shared above the tabs"
Assert-Contains $tw 'Next Up' "next up stays shared"
Assert-Contains $tw 'Continue Reading' "continue stays shared"
Assert-Contains $tw 'ComicsCatalog.shelf' "world still owns the one-time shelf compute"
Assert-Contains $tw 'GcApi.explore' "world still owns the one-time explore fetch"
Assert-Contains $tw 'onMangaSeriesRequested' "Discover manga card routes to the existing manga series door (Task 7)"
Assert-Contains $tw 'onComicSeriesRequested' "Discover comics card routes to the existing comic series door (Task 7)"
Assert-Contains $tw 'showExplicitContent: tanko.showExplicitContent' "Discover reads the inherited explicit preference (Task 7 Step 4)"
Assert-Absent  $tw 'title: "Top in Tankoban — Manga"' "manga row moved out of the world into the tab"
Assert-Absent  $tw 'title: "Top in Tankoban — Comics"' "comics row moved out of the world into the tab"
Assert-Absent  $tw 'title: "Explore Comics"' "explore-comics moved out of the world into the tab"

Write-Host "tankoban tabs contract OK"
