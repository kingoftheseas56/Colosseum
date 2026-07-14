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

$top = Read-RepoFile "qml/TrendingTop10.qml"
$world = Read-RepoFile "qml/TankobanWorld.qml"
$main = Read-RepoFile "qml/Main.qml"

Assert-Contains $top 'signal exploreClicked()' `
    "TrendingTop10 must expose the existing Explore label as an event."
Assert-Contains $top 'onMoreClicked: top10.exploreClicked()' `
    "TrendingTop10 Explore label must emit exploreClicked."
Assert-Contains $world 'signal comicCatalogRequested(var rows)' `
    "TankobanWorld must expose the full-catalog route."
Assert-Contains $world 'items: tanko.comicRows.slice(0, 10)' `
    "Tankoban home Top Comics must display exactly ten resolved rows."
Assert-Contains $world 'var topComics = tanko.comicRows.slice(0, 10)' `
    "Top Comics clicks must resolve against the same ten-row slice."
Assert-Contains $world 'tanko.comicCatalogRequested(tanko.comicRows)' `
    "Top Comics Explore must receive the unsliced full catalog."
Assert-Contains $main 'function openComicCatalog(rows)' `
    "Main must expose a lazy catalog opener."
Assert-Contains $main 'function closeComicCatalog()' `
    "Main must expose a catalog closer."
Assert-Contains $main 'var comicCatalogSignal = item["comicCatalogRequested"]' `
    "Main must discover the additive Tankoban catalog signal safely."
Assert-Contains $main 'comicCatalogSignal.connect(win.openComicCatalog)' `
    "Tankoban Explore must route to the catalog Loader."
Assert-Contains $main 'id: comicCatalogLayer' "Main must own the catalog layer."
Assert-Contains $main 'source: "ComicCatalogPage.qml"' "The catalog layer must remain lazy."
Assert-Contains $main 'item.seriesRequested.connect(win.openComicSeries)' `
    "Catalog cards must reuse the existing series route."
if ($main.IndexOf('else if (comicSeriesLayer.active) win.closeComicSeries()') -ge `
        $main.IndexOf('else if (comicCatalogLayer.active) win.closeComicCatalog()')) {
    throw "Escape must close a comic series before the catalog beneath it."
}
if ($main.IndexOf('else if (comicCatalogLayer.active) win.closeComicCatalog()') -ge `
        $main.IndexOf('else if (worldStack.current !== "") win.closeWorld()')) {
    throw "Escape must close the catalog before leaving Tankoban."
}
if ($main.Contains('import "comics_db.gen.js" as ComicsDbData') `
        -or $main.Contains('ComicsDb.setData(ComicsDbData.data)')) {
    throw "Root Main.qml must remain free of generated catalog ingest."
}

$pagePath = Join-Path $root "qml/ComicCatalogPage.qml"
if (!(Test-Path -LiteralPath $pagePath)) {
    throw "ComicCatalogPage.qml must implement the ranked library wall."
}
$page = Get-Content -Raw -LiteralPath $pagePath
Assert-Contains $page 'GridView {' "The 688-card wall must be virtualized with GridView."
Assert-NotContains $page 'Repeater {' "The catalog page must not eagerly repeat all 688 cards."
Assert-Contains $page 'CatalogModel.prepare(rows, ComicsDb.hasDownloadableEdition)' `
    "The wall must annotate honest availability once."
Assert-Contains $page 'CatalogModel.filter(catalogRows, query, downloadableOnly)' `
    "The wall must use the tested local filter."
Assert-Contains $page 'function applyView(nextQuery, nextDownloadableOnly)' `
    "The wall must centralize filter transitions for scroll restoration."
Assert-Contains $page 'savedAllContentY = catalogGrid.contentY' `
    "Entering a filtered view must save the unfiltered scroll position."
Assert-Contains $page 'catalogGrid.contentY = Math.max(0, savedAllContentY)' `
    "Clearing the view must restore the unfiltered scroll position."
Assert-Contains $page 'text: card.modelData.displayRank' `
    "Cards must show their canonical clean display rank."
Assert-Contains $page 'asynchronous: true' "Cover loading must be asynchronous."
Assert-Contains $page 'text: "Downloadable"' "The honest availability filter must be visible."
Assert-Contains $page 'placeholderText: "Search comics"' "The sticky search field must be visible."
Assert-Contains $page 'text: "Catalog unavailable"' "The empty catalog state must be explicit."
Assert-Contains $page 'text: "No comics match this view"' "The filtered empty state must be explicit."

$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_QUICK_CONTROLS_STYLE = "Basic"
$harness = Join-Path $PSScriptRoot "comic_catalog_page_harness.qml"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0 -or $output -notlike "*COMIC_CATALOG_PAGE_OK*") {
    throw "Comic catalog page harness failed (exit $LASTEXITCODE):`n$output"
}

Write-Host "top comics explore contracts: OK"
