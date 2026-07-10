$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# Needles are ASCII-only on purpose: PowerShell 5.1 may read this .ps1 as Windows-1252,
# which would mangle any UTF-8 middot/em-dash in a needle and break the -like match.
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
$sources = Get-Content (Join-Path $root "qml/ComicSources.js") -Raw
Assert-Contains $sources '.import "XoxoApi.js" as Xoxo' "registry must import xoxo (leading-dot .import - plain import kills the file)"
Assert-Contains $sources '.import "ComicsApi.js" as GetComics' "registry must import getcomics"
Assert-Contains $sources 'key: "xoxo"' "xoxo registered"
Assert-Contains $sources 'kind: "issues"' "xoxo kind routes the issue-based flow"
Assert-Contains $sources 'key: "getcomics"' "getcomics registered"
Assert-Contains $sources 'kind: "archives"' "getcomics kind routes the archive flow"
$search = Get-Content (Join-Path $root "qml/WorldSearch.js") -Raw
Assert-Contains $search 'function searchLocg' "comics search fan-out must ride the LOCG catalogue lane"
Assert-Contains $search 'GetComics"' "getcomics results carry their own group label"
Assert-Contains $search 'data: { locg: true' "locg results must carry routing data"
$dl = Get-Content (Join-Path $root "native/engine/MangaDownloader.h") -Raw
Assert-Contains $dl 'downloadPages' "explicit-URL entry point must exist for xoxo issues"
Assert-Contains $dl 'presetPages' "Job must carry preset pages (skips the WeebCentral resolver)"
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Contains $main 'xoxocomic.com' "xoxo host must be IPv4-pinned (Qt-on-Windows dead-IPv6 stall)"
$world = Get-Content (Join-Path $root "qml/TankobanWorld.qml") -Raw
Assert-Contains $world 'LocgApi' "world page must feed comics rows from the LOCG catalogue (Task 7 re-drive)"
Assert-Contains $world 'GetComics Archives' "GetComics keeps its explore door"
if (!(Test-Path (Join-Path $root "qml/XoxoGenrePage.qml"))) { throw "MISSING: XoxoGenrePage.qml" }
if (!(Test-Path (Join-Path $root "qml/ComicArchiveBoard.qml"))) { throw "MISSING: ComicArchiveBoard.qml" }
Assert-Contains $world 'e.source = src' "continue tiles must tag the comic source (xoxo vs getcomics)"
# The comic session/continue/downloads routes split THREE lanes by id prefix
# (xoxo: / gc: / manga fallback) — review fix 2026-07-09: xoxo ids must never
# fall through to the manga layer (wrong page on resume, stranded surface on minimize).
$mainQml = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Assert-Contains $mainQml 'function openXoxoSeriesAt' "resume-into-reader route must exist for xoxo"
Assert-Contains $mainQml 'tsid.indexOf("xoxo:") === 0) xoxoSeriesLayer.active = false' "session teardown must know the xoxo lane"
Assert-Contains $mainQml 'csid.indexOf("xoxo:") === 0 ? xoxoSeriesLayer' "session capture must know the xoxo lane"
# reader chrome (minimize/close) + global back must ALL know the xoxo lane — eyes-on
# 2026-07-09 found minimize/close dead on the xoxo reader (review under-scoped these).
Assert-Contains $mainQml 'x && x.openChapterId' "minimizeComicReader must register the xoxo reader"
Assert-Contains $mainQml 'xoxoSeriesLayer.item.openChapterId.length) win.closeXoxoSeries' "closeComicReader must tear down the xoxo reader"
Assert-Contains $mainQml 'xoxoSeriesLayer.active) win.closeXoxoSeries' "global back must close the xoxo series page"
# downloaded pages must be validated as real images (soft-block HTML served as .jpg)
$dlcpp = Get-Content (Join-Path $root "native/engine/MangaDownloader.cpp") -Raw
Assert-Contains $dlcpp 'looksLikeImage' "downloader must validate a page is a real image, never save HTML"
# Spec A cooldown: honest blocked states everywhere (eyes-on 2026-07-09 — no more
# homepage garbage parsed as real content).
if (!(Test-Path (Join-Path $root "qml/SourceCooldownBanner.qml"))) { throw "MISSING: SourceCooldownBanner.qml" }
$xs3 = Get-Content (Join-Path $root "qml/XoxoSeries.qml") -Raw
Assert-Contains $xs3 'SourceCooldownBanner' "series page must show the cooldown banner"
# Plan B: series page shows parsed metadata + reads issues ascending (#1 first)
Assert-Contains $xs3 'page.seriesMeta' "series hero must show parsed metadata"
Assert-Contains $xs3 'issuesRaw.slice().reverse()' "display issues must be ascending (#1 first); reader stays newest-first"
$xg2 = Get-Content (Join-Path $root "qml/XoxoGenrePage.qml") -Raw
Assert-Contains $xg2 'SourceCooldownBanner' "genre page must show the cooldown banner"
Assert-Contains $xg2 'sortMode' "genre page must have a Newest/A-Z sort control"
$ws3 = Get-Content (Join-Path $root "qml/WorldSearch.js") -Raw
Assert-Contains $ws3 'notice: true' "a blocked xoxo lane must emit a notice row, never a silent empty group"
Assert-Contains $mainQml 'data.notice' "search-open must ignore notice rows"
Assert-Contains $mainQml 'Xoxo.nowFn' "the live clock must be set for the cooldown machine"
# Spec A A3/QSettings: downloads pause-and-resume on soft-block (not 3 fast retries into
# the wall); org name set so every QML Settings block actually persists.
$main3 = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Contains $main3 'setOrganizationName' "org name must be set so QML Settings persist"
$dl3 = Get-Content (Join-Path $root "native/engine/MangaDownloader.cpp") -Raw
Assert-Contains $dl3 'coolWaves' "an HTML-blocked xoxo page must pause the job, not burn fast retries"
# --- GetComics->LOCG content lane (spec 2026-07-10): xoxo dead, GetComics is the
#     content source behind the LOCG catalogue. matchIssues attaches GC posts onto
#     LOCG rows; unmatched rows stay honest; collections get their own shelf. ---
$resolveJs = Get-Content (Join-Path $root "qml/ComicResolve.js") -Raw
Assert-Contains $resolveJs 'function matchIssues' "issue-level attach (LOCG rows <-> GC posts) must exist"
Assert-Contains $mainQml 'comicResolveV3' "resolve store must be the V3 namespace (stale xoxo mappings orphaned)"
Assert-Contains $mainQml 'GcApi.searchSeries' "resolve injection must search GetComics, not dead xoxo"
Assert-Contains $xs3 'Comics.downloadIssue' "gcMode verbs must ride the ComicDownloader"
Assert-Contains $xs3 'Collected editions' "TPB/Omnibus collections shelf must exist"
Assert-Contains $xs3 'Not on GetComics yet' "unmatched issue rows must be honest, not a fake verb"
Assert-Contains $xs3 'western: page.gcMode' "reader must flip to the Comics store in gcMode"
# three-lane routes (twinned-loom): locg: catalogue, gc: content, xoxo: legacy downloads
# (xoxo: routes are already asserted above; here we lock the other two lanes)
Assert-Contains $mainQml 'locg:' "Main.qml must route locg: catalogue ids"
Assert-Contains $mainQml 'gc:' "Main.qml must route gc: content ids"
# the blocked-mirror drop is a real behavioral test (comic_dls_parse_harness), not a grep needle
Write-Host "test_comics_sources_p0 PASS"
