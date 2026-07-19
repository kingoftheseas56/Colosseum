$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$archive = Join-Path $root 'archive/universes/custom-five-2026-07'
$pages = @(
    'OnePieceUniversePage.qml',
    'CinematicPage.qml',
    'DragonBallUniversePage.qml',
    'CosmereUniversePage.qml',
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
    if (Test-Path (Join-Path $root "qml/$page")) {
        throw "$page remains live under qml/"
    }
    if ($manifest -notmatch [regex]::Escape("qml/$page")) {
        throw "$page is absent from the restoration manifest"
    }
}

$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
foreach ($needle in @(
    'import "Universes.js" as Universes',
    'import "UniverseApi.js" as UniverseApi',
    'import "McuApi.js" as Mcu',
    'function universeSourceFor(',
    'function openUniverse(',
    'function openUniverseHall(',
    'id: universeHallLayer',
    'id: universeLayer',
    'id: universeWarmer',
    'id: heroView',
    'Explore the universe'
)) {
    if ($main.Contains($needle)) { throw "live universe seam remains: $needle" }
}
foreach ($page in $pages) {
    if ($main.Contains($page)) { throw "Main still references archived page $page" }
}

$continueAt = $main.IndexOf('id: contCol')
$firstWidgetAt = $main.IndexOf('Bookshelf {')
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
