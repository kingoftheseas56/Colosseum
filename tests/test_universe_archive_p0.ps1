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

# AF2 Home redesign (2026-07-19): Continue is now the first HomeRail on the glass board,
# ahead of the world rails; its seam moved from the ContinueTile home variant to HomeRail.
$continueAt = $main.IndexOf('railTitle: "Continue"')
$firstWorldRail = $main.IndexOf('worldTag: "Theatre"')
if ($continueAt -lt 0 -or $firstWorldRail -lt 0 -or $continueAt -gt $firstWorldRail) {
    throw 'universal Continue must be the first logical Home section'
}
foreach ($needle in @(
    'Progress.recent("", 12)',
    'onSeeAll: win.openContinueSeeAll("home")',
    'win.resumeContinue(m.entry)'
)) {
    if (-not $main.Contains($needle)) { throw "Continue seam changed or disappeared: $needle" }
}

Write-Host 'universe archive + Continue-first contract: PASS'
