# MAL genre-catalog revival P0 (2026-07-18): the genre pages read the baked
# Kaggle MAL dump first, and the live Jikan/AniList/Kitsu ladder survives as
# the fallback beneath it.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# the bake pipeline exists and the seam is registered
if (!(Test-Path (Join-Path $root 'scripts/anime_brain/build_mal_db.py'))) { throw 'bake script missing' }
$main = Get-Content (Join-Path $root 'native/main.cpp') -Raw
if ($main -notmatch 'MalCatalog.*data/mal_catalog\.db') { throw 'MalCatalog not registered in main.cpp' }
$cmake = Get-Content (Join-Path $root 'native/CMakeLists.txt') -Raw
if ($cmake -notmatch 'engine/MalCatalog\.cpp') { throw 'MalCatalog.cpp not in the app target' }

# both lanes go catalog-first...
foreach ($f in @('qml/TheatreGenreApi.js', 'qml/GenreApi.js')) {
    $src = Get-Content (Join-Path $root $f) -Raw
    if ($src -notmatch 'MalCatalog[\s\S]{0,40}ready\(\)') { throw "$f lost the baked-catalog-first path" }
    if ($src -notmatch 'genreEntries\(') { throw "$f no longer queries the baked catalog" }
    # ...and the LIVE ladder must survive beneath (a fresh machine has no db)
    if ($src -notmatch 'api\.jikan\.moe') { throw "$f lost its live Jikan fallback" }
}
$tga = Get-Content (Join-Path $root 'qml/TheatreGenreApi.js') -Raw
if ($tga -notmatch 'graphql\.anilist\.co') { throw 'TheatreGenreApi lost the AniList rung' }
$ga = Get-Content (Join-Path $root 'qml/GenreApi.js') -Raw
if ($ga -notmatch 'kitsuGenre') { throw 'GenreApi lost the Kitsu rung' }

# when the local artifact exists, it must actually answer (both mediums, real tags)
$db = Join-Path $root 'data/mal_catalog.db'
if (Test-Path $db) {
    $py = @"
import sqlite3
d = sqlite3.connect(r'$db')
q = "SELECT (SELECT COUNT(1) FROM tag_count WHERE medium='anime'), (SELECT COUNT(1) FROM tag_count WHERE medium='manga'), (SELECT COUNT(1) FROM anime), (SELECT COUNT(1) FROM manga)"
print(d.execute(q).fetchone())
"@
    $probe = $py | python -
    if ($LASTEXITCODE -ne 0) { throw 'baked db unreadable' }
    $nums = [regex]::Matches($probe, '\d+') | ForEach-Object { [int]$_.Value }
    if ($nums[0] -lt 50 -or $nums[1] -lt 50) { throw "baked db has too few tags: $probe" }
    if ($nums[2] -lt 10000 -or $nums[3] -lt 10000) { throw "baked db has too few rows: $probe" }
    Write-Host "  baked db live: $probe (anime tags, manga tags, anime rows, manga rows)"
} else {
    Write-Host '  baked db absent - fallback lane covers (run scripts/anime_brain/build_mal_db.py)'
}

Write-Host 'test_mal_genre_catalog_p0: PASS (catalog-first both lanes, live ladder intact)'
