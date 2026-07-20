# Window chrome carries the fullscreen/windowed toggle app-wide (Hemanth, live,
# 2026-07-20: "we removed the fullscreen rule long ago" — the old fullscreen-only
# doctrine is dead; the honest cluster is minimize · fullscreen-toggle · power).
# Sweep: 25 page surfaces gain `signal fullscreenRequested()` + a control emitting
# it; the 3 pages that had NO window buttons at all (Continue See-all, Search,
# Biblio search) gain the full cluster; Main.qml routes every page's toggle into
# win.toggleFullscreenShell (the same shell flip as F11 / the Home TopBar).
# WallpaperSearch carries the cluster too (Hemanth 2026-07-20: it covers the shell top
# bar, so it must). Its power button emits quitRequested (closeRequested = the panel "x").

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

# Literal substring check (needles may carry [] / {} — never use -like here).
function Assert-Contains($text, $needle, $message) { if (-not $text.Contains($needle)) { throw $message } }

$pages = @(
    "GenrePage", "GenreIndex", "BiblioGenreIndex", "BiblioGenrePage",
    "TheatreGenrePage", "TheatreGenreIndex", "MangaSeries", "ComicSeries",
    "ComicSeriesPage", "LocgPublisherPage", "ComicArchiveBoard", "ComicArchiveIndex",
    "TheatreSeries", "DownloadsPage", "UniverseHallPage", "GalaxyUniversePage",
    "SagaUniversePage", "EraUniversePage", "StudioUniversePage", "UniversePage",
    "ExtensionsPage", "BiblioBook",
    "ContinueSeeAllPage", "SearchSurface", "BiblioSearch"
)

foreach ($p in $pages) {
    $qml = Get-Content (Join-Path $root "qml/$p.qml") -Raw
    Assert-Contains $qml 'signal fullscreenRequested()' "$p must declare fullscreenRequested."
    Assert-Contains $qml '.fullscreenRequested()' "$p must EMIT fullscreenRequested from a drawn control."
}

# The three former gap pages must now draw the standard cluster (they had NO
# window buttons at all — declared signals, dead air).
foreach ($p in @("ContinueSeeAllPage", "SearchSurface", "BiblioSearch")) {
    $qml = Get-Content (Join-Path $root "qml/$p.qml") -Raw
    Assert-Contains $qml 'minimize.svg' "$p must draw the minimize button."
    Assert-Contains $qml 'power.svg' "$p must draw the power button."
    Assert-Contains $qml '.minimizeRequested()' "$p must emit minimizeRequested from its button."
    Assert-Contains $qml '.closeRequested()' "$p must emit closeRequested from its button."
}

# WallpaperSearch is a full-bleed overlay that covers the shell top bar, so it carries its
# own cluster: minimize + fullscreen-toggle + power (power = quitRequested, since its "x"
# already means closeRequested = back to the world).
$wp = Get-Content (Join-Path $root "qml/WallpaperSearch.qml") -Raw
Assert-Contains $wp 'signal fullscreenRequested()' "WallpaperSearch must declare fullscreenRequested."
Assert-Contains $wp 'signal minimizeRequested()' "WallpaperSearch must declare minimizeRequested."
Assert-Contains $wp 'signal quitRequested()' "WallpaperSearch power button must quit the app."
Assert-Contains $wp 'minimize.svg' "WallpaperSearch must draw the minimize button."
Assert-Contains $wp 'power.svg' "WallpaperSearch must draw the power button."
Assert-Contains $wp 'root.fullscreenRequested()' "WallpaperSearch must emit the fullscreen toggle."

# Host: every page loader routes the toggle into the one shell flip.
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw
$connects = ([regex]::Matches($main, [regex]::Escape('fullscreenRequested.connect(win.toggleFullscreenShell)'))).Count
if ($connects -lt 22) { throw "Main.qml must connect fullscreenRequested on all page loaders (found $connects, need >= 22: 19 pages + player + reader + wallpaper)." }

# The dead rule must not survive as a code comment steering future chrome.
$bib = Get-Content (Join-Path $root "qml/BiblioBook.qml") -Raw
if ($bib.Contains("fullscreen-only: no maximize")) { throw "BiblioBook still carries the dead fullscreen-only comment." }

Write-Host "PASS: fullscreen toggle present across all 25 page surfaces, gap pages clustered, host wired."
