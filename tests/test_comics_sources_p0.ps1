$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}
# xoxo is RETIRED (scorched earth, spec 2026-07-12): no api, no genre page, no registry,
# no fixtures, no C++ seam, no pin. The comics lane is LOCG (brain) + GetComics (content).
if (Test-Path (Join-Path $root "qml/XoxoApi.js")) { throw "STALE: XoxoApi.js must be deleted" }
if (Test-Path (Join-Path $root "qml/XoxoGenrePage.qml")) { throw "STALE: XoxoGenrePage.qml must be deleted" }
if (Test-Path (Join-Path $root "qml/ComicSources.js")) { throw "STALE: ComicSources.js (registry of one, no consumer) must be deleted" }
if (Test-Path (Join-Path $root "tests/fixtures/xoxo")) { throw "STALE: xoxo fixtures must be deleted" }
$search = Get-Content (Join-Path $root "qml/WorldSearch.js") -Raw
# Search rides the availability-first catalogue ONLY (spec 2026-07-18): the curated
# shortlist lane, the live GetComics lane, and the parked LOCG lane are written over.
Assert-Contains $search 'searchCatalogDb' "the catalogue is the comics search lane"
Assert-Contains $search 'data: { gcd: true' "catalogue results must carry gcd routing data"
Assert-Absent  $search 'searchWestern' "live GetComics search lane retired 2026-07-18"
Assert-Absent  $search 'searchLocg' "parked LOCG lane deleted from the fan-out 2026-07-18 (LocgApi.js file stays parked)"
Assert-Absent  $search 'ComicsApi' "search must not import the live GetComics api"
$dl = Get-Content (Join-Path $root "native/engine/MangaDownloader.h") -Raw
Assert-Absent $dl 'downloadPages' "preset-pages seam retired with xoxo (sole consumer)"
Assert-Absent $dl 'presetPages' "preset-pages seam retired with xoxo (sole consumer)"
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Absent $main 'xoxocomic.com' "dead source must not be IPv4-pinned"
Assert-Contains $main 'getcomics.org' "GC API host publishes AAAA records - unpinned = dead-IPv6 stall killed every attach (2026-07-12)"
Assert-Contains $main 'leagueofcomicgeeks.com' "LOCG catalogue host publishes AAAA records - same stall, same pin"
Assert-Contains $main 'AppleWebKit' "NAM must stamp a browser UA - QML XHR silently drops User-Agent (restricted header), and LOCG 403s bot UAs (2026-07-12)"
Assert-Absent $main 'PAGES_SELFTEST' "preset-pages selftest lane retired with the seam"
$world = Get-Content (Join-Path $root "qml/TankobanWorld.qml") -Raw
Assert-Absent $world 'LocgApi' "world page is GC-driven; LOCG parked 2026-07-12 (GetComics = brain AND content)"
Assert-Contains $world 'GcApi.explore' "explore mosaic rides GetComics' own tag taxonomy inline"
Assert-Contains $world 'westernExploreRequested' "an explore box opens the archive index (middle layer, never raw feeds)"
Assert-Contains $world 'e.source = src' "continue tiles must tag the comic source (GetComics)"
$shelf = Get-Content (Join-Path $root "qml/ComicSeries.qml") -Raw
Assert-Contains $shelf 'tagBySlug' "the GC shelf resolves its tag SLUG-FIRST (flooded search buried popular titles, 2026-07-12)"
$locgparked = Get-Content (Join-Path $root "qml/LocgApi.js") -Raw
Assert-Contains $locgparked 'PARKED 2026-07-12' "LOCG kept in-tree dormant, never deleted (Hemanth call)"
# comic session/continue routes are TWO lanes now: gc: and manga fallback.
$mainQml = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Assert-Absent $mainQml 'xoxo:' "no xoxo id-prefix routing may remain"
Assert-Contains $mainQml 'indexOf("gc:") === 0' "gc: lane routing must survive the cut"
$dlcpp = Get-Content (Join-Path $root "native/engine/MangaDownloader.cpp") -Raw
Assert-Contains $dlcpp 'looksLikeImage' "magic-bytes-first image acceptance is generic honesty and must survive the xoxo cut"
Assert-Contains $main 'setOrganizationName' "QSettings org identity (Brotherhood) must survive - progress/resolve stores key off it"
# live-lane needles: the comic series page and its resolve wiring (renamed off the retired
# source name to ComicSeriesPage 2026-07-12) must keep their load-bearing verbs and strings.
$series = Get-Content (Join-Path $root "qml/ComicSeriesPage.qml") -Raw
Assert-Contains $series 'Comics.downloadIssue' "the series page's one download verb (GetComics archives)"
Assert-Contains $series 'Collected editions' "collections shelf must survive"
Assert-Contains $series 'Not on GetComics yet' "honest-dim rule: unmatched rows get no fake verb"
Assert-Contains $mainQml 'comicResolveV3' "attach store version pin"
Assert-Contains $mainQml 'GcApi.searchSeries' "resolve searchFn wiring (GetComics)"
Assert-Contains $mainQml 'Resolve.slugFn' "slug-first resolve wiring (top-10 empty-series fix 2026-07-12)"
Assert-Contains $mainQml 'GcApi.tagBySlug' "exact-slug tag lookup feeds the slug lane"
$resolve = Get-Content (Join-Path $root "qml/ComicResolve.js") -Raw
Assert-Contains $resolve 'slugFn' "resolve machine must try the exact slug before the flooded search"
# scorched earth: zero xoxo references anywhere in the source tree (this test excepted).
$hits = Get-ChildItem -Recurse (Join-Path $root "qml"), (Join-Path $root "native"), (Join-Path $root "tests") -Include *.qml,*.js,*.cpp,*.h,*.ps1 |
    Where-Object { $_.FullName -notmatch 'build-msvc|build-smoke' } |
    Select-String -Pattern "xoxo" -SimpleMatch -CaseSensitive:$false |
    Where-Object { $_.Filename -ne "test_comics_sources_p0.ps1" }
if ($hits) { $hits | ForEach-Object { Write-Host $_.Path $_.LineNumber }; throw "STALE: xoxo references remain" }
Write-Host "comics-sources P0 contract OK"
