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
Assert-Contains $search 'function searchLocg' "comics search fan-out must ride the LOCG catalogue lane"
Assert-Contains $search 'GetComics"' "getcomics results carry their own group label"
Assert-Contains $search 'data: { locg: true' "locg results must carry routing data"
$dl = Get-Content (Join-Path $root "native/engine/MangaDownloader.h") -Raw
Assert-Absent $dl 'downloadPages' "preset-pages seam retired with xoxo (sole consumer)"
Assert-Absent $dl 'presetPages' "preset-pages seam retired with xoxo (sole consumer)"
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Absent $main 'xoxocomic.com' "dead source must not be IPv4-pinned"
Assert-Absent $main 'PAGES_SELFTEST' "preset-pages selftest lane retired with the seam"
$world = Get-Content (Join-Path $root "qml/TankobanWorld.qml") -Raw
Assert-Contains $world 'LocgApi' "world page must feed comics rows from the LOCG catalogue"
Assert-Contains $world 'GetComics Archives' "GetComics keeps its explore door"
Assert-Contains $world 'e.source = src' "continue tiles must tag the comic source (GetComics)"
# comic session/continue routes are TWO lanes now: gc: and manga fallback.
$mainQml = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Assert-Absent $mainQml 'xoxo:' "no xoxo id-prefix routing may remain"
Assert-Contains $mainQml 'indexOf("gc:") === 0' "gc: lane routing must survive the cut"
$dlcpp = Get-Content (Join-Path $root "native/engine/MangaDownloader.cpp") -Raw
Assert-Contains $dlcpp 'looksLikeImage' "magic-bytes-first image acceptance is generic honesty and must survive the xoxo cut"
Assert-Contains $main 'setOrganizationName' "QSettings org identity (Brotherhood) must survive - progress/resolve stores key off it"
Write-Host "comics-sources P0 contract OK"
