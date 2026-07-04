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

$expectedRows = @(
    "Airing Now",
    "Top Airing on MAL",
    "Upcoming Season",
    "Top Series on MAL",
    "Top Movies on MAL",
    "Most Popular on MAL",
    "Top Rated on MAL",
    "Hidden Gems on MAL",
    "2020s Hits",
    "2010s Classics",
    "2000s Era",
    "Foundation Years (90s)",
    "Action & Adventure",
    "Romance",
    "Slice of Life",
    "Mecha",
    "Fantasy",
    "Sci-Fi",
    "Psychological",
    "Horror & Supernatural"
)

foreach ($row in $expectedRows) {
    if ($api -notmatch [regex]::Escape("title: `"$row`"")) {
        throw "Missing Harbor anime row: $row"
    }
}

Write-Host "Theatre anime parity structure OK."
