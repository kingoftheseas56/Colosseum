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
Write-Host "test_comics_sources_p0 PASS"
