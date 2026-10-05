$ErrorActionPreference = 'Stop'
# 2026-10-05 repair (Arc 58): the custom-five ARCHIVE contract still stands — every page
# preserved verbatim, listed in the restoration manifest — but two of the five are back in
# the live product BY DESIGN (One Piece and Cosmere ride Arc 58's universe machinery:
# bespoke pages in universeLayer's source chain, no extension imports). So "must not be
# live" now guards only the three that are still archived. The live-seam block guards the
# OLD custom-five glue that must never return (the baked imports, universeSourceFor,
# universeWarmer); the rest of that seam (openUniverse, universeLayer, heroView, the
# tribute door) is legitimate Arc 58 surface and is no longer forbidden here. The
# Continue-first marker moved with Home's lazy loaders (homeBookshelfLoader, not Bookshelf {).
$root = Split-Path -Parent $PSScriptRoot
$archive = Join-Path $root 'archive/universes/custom-five-2026-07'
# every page ever moved into the custom-five archive: preserved + manifest-listed, always
$pages = @(
    'OnePieceUniversePage.qml',
    'CinematicPage.qml',
    'DragonBallUniversePage.qml',
    'CosmereUniversePage.qml',
    'MagazineUniversePage.qml'
)
# the pages that must STILL be archive-only (not live under qml/, not referenced by Main)
$stillArchived = @(
    'CinematicPage.qml',
    'DragonBallUniversePage.qml',
    'MagazineUniversePage.qml'
)

if (-not (Test-Path (Join-Path $archive 'README.md'))) {
    throw 'custom-five universe archive manifest is missing'
}
$manifest = Get-Content (Join-Path $archive 'README.md') -Raw
foreach ($page in $pages) {
    if (-not (Test-Path (Join-Path $archive "qml/$page"))) {
        throw "$page is not preserved in the custom-five archive"
    }
    if ($manifest -notmatch [regex]::Escape("qml/$page")) {
        throw "$page is absent from the restoration manifest"
    }
}
foreach ($page in $stillArchived) {
    if (Test-Path (Join-Path $root "qml/$page")) {
        throw "$page remains live under qml/ (only One Piece and Cosmere returned, Arc 58)"
    }
}

$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
foreach ($needle in @(
    'import "Universes.js" as Universes',
    'import "UniverseApi.js" as UniverseApi',
    'import "McuApi.js" as Mcu',
    'function universeSourceFor(',
    'id: universeWarmer'
)) {
    if ($main.Contains($needle)) { throw "live universe seam remains: $needle" }
}
foreach ($page in $stillArchived) {
    if ($main.Contains($page)) { throw "Main still references archived page $page" }
}

$continueAt = $main.IndexOf('id: contCol')
$firstWidgetAt = $main.IndexOf('id: homeBookshelfLoader')
if ($continueAt -lt 0 -or $firstWidgetAt -lt 0 -or $continueAt -gt $firstWidgetAt) {
    throw 'universal Continue must be the first logical Home section'
}
foreach ($needle in @(
    'Progress.recent("", 12)',
    'onMoreClicked: win.openContinueSeeAll("home")',
    'onResumeRequested: win.resumeContinue(modelData)',
    'onDetailRequested: win.detailContinue(modelData)',
    'onRemoveRequested: Progress.forget(modelData.kind, modelData.id)'
)) {
    if (-not $main.Contains($needle)) { throw "Continue seam changed or disappeared: $needle" }
}

Write-Host 'universe archive + Continue-first contract: PASS'
