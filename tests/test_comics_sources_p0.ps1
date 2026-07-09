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
Assert-Contains $search 'function searchXoxo' "xoxo search fan-out must exist"
Assert-Contains $search 'XOXO"' "xoxo results carry their own group label ending in XOXO"
Assert-Contains $search 'GetComics"' "getcomics results carry their own group label"
Assert-Contains $search 'data: { xoxo: true' "xoxo results must carry routing data"
$dl = Get-Content (Join-Path $root "native/engine/MangaDownloader.h") -Raw
Assert-Contains $dl 'downloadPages' "explicit-URL entry point must exist for xoxo issues"
Assert-Contains $dl 'presetPages' "Job must carry preset pages (skips the WeebCentral resolver)"
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Contains $main 'xoxocomic.com' "xoxo host must be IPv4-pinned (Qt-on-Windows dead-IPv6 stall)"
$world = Get-Content (Join-Path $root "qml/TankobanWorld.qml") -Raw
Assert-Contains $world 'XoxoApi' "world page must feed comics rows from xoxo"
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
Write-Host "test_comics_sources_p0 PASS"
