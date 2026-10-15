$ErrorActionPreference = "Stop"

$apiPath = Join-Path $PSScriptRoot "..\qml\TheatreApi.js"
$api = Get-Content $apiPath -Raw

$jpgLarge = $api.IndexOf("meta.images.jpg.large_image_url")
$webpLarge = $api.IndexOf("meta.images.webp.large_image_url")
if ($jpgLarge -lt 0 -or $webpLarge -lt 0 -or $jpgLarge -gt $webpLarge) {
    throw "TheatreApi.jikanPoster must prefer JPG over WebP because this Qt build cannot decode MAL WebP covers."
}

if ($api -notmatch 'var\s+JIKAN_CACHE_TTL_MS\s*=') {
    throw "TheatreApi should keep a short Jikan cache TTL."
}
if ($api -notmatch 'var\s+jikanInflight\s*=\s*\{\}') {
    throw "TheatreApi should coalesce duplicate in-flight Jikan requests."
}
if ($api -notmatch 'requestJsonCached\(JIKAN \+ path') {
    throw "TheatreApi.jikanQuery should use cached/coalesced Jikan requests."
}

# The deep anime inventory (spec 2026-08-01) now lives in TheatreCatalogRules.js — the pure
# shelf definitions TheatreApi.js consumes for the Anime tab. This supersedes the old flat
# animeSpecs titles that used to sit inline in TheatreApi.js; the depth is broader, not weaker.
$rulesPath = Join-Path $PSScriptRoot "..\qml\TheatreCatalogRules.js"
$rules = Get-Content $rulesPath -Raw

$expectedRows = @(
    "Top 10",
    "Trending",
    "Airing Now",
    "Top Airing",
    "Upcoming Season",
    "Top Series",
    "Top Anime Movies",
    "Most Popular",
    "Top Rated",
    "Hidden Gems",
    "2020s Anime",
    "2010s Anime",
    "2000s Anime",
    "1990s and Earlier",
    "Action and Adventure",
    "Romance",
    "Slice of Life",
    "Mecha",
    "Fantasy",
    "Science Fiction",
    "Psychological",
    "Horror and Supernatural"
)

foreach ($row in $expectedRows) {
    if ($rules -notmatch [regex]::Escape("`"$row`"")) {
        throw "Missing deep anime row: $row"
    }
}

Write-Host "Theatre anime parity structure OK."
