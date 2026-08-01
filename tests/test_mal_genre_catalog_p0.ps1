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

# Tankoban Discover (2026-08-01): the paged manga discovery seam + its axis-aware
# bake tables must exist. Static (source-level) so it holds with or without a db.
$malh = Get-Content (Join-Path $root 'native/engine/MalCatalog.h') -Raw
if ($malh -notmatch 'discoverPage') { throw 'MalCatalog.h missing discoverPage (Tankoban Discover)' }
if ($malh -notmatch 'discoverFilters') { throw 'MalCatalog.h missing discoverFilters (Tankoban Discover)' }
$bake = Get-Content (Join-Path $root 'scripts/anime_brain/build_mal_db.py') -Raw
if ($bake -notmatch 'CREATE TABLE classification') { throw 'build_mal_db.py no longer creates the classification table' }
if ($bake -notmatch 'CREATE TABLE classification_count') { throw 'build_mal_db.py no longer creates classification_count' }

# Both lanes go catalog-first via a PASSED-IN catalog param, never a bare global.
# A .pragma library script cannot see context properties: a MalCatalog global is
# always undefined there (the 2026-07-18 "still on AniList" bug). The page passes it in.
foreach ($f in @('qml/TheatreGenreApi.js', 'qml/GenreApi.js')) {
    $src = Get-Content (Join-Path $root $f) -Raw
    if ($src -notmatch 'function loadGenre\(.*catalog') {
        throw "$f loadGenre must take a catalog param (pragma-library cannot see the global)"
    }
    if ($src -match 'MalCatalog\.ready' -or $src -match 'MalCatalog\.genreEntries') {
        throw "$f references the MalCatalog global directly; use the passed-in catalog"
    }
    if ($src -notmatch 'catalog\.ready\(\)') { throw "$f lost the passed-in catalog-first guard" }
    if ($src -notmatch 'catalog\.genreEntries\(') { throw "$f no longer queries the passed-in catalog" }
    if ($src -notmatch 'api\.jikan\.moe') { throw "$f lost its live Jikan fallback" }
}

# The PAGES must actually hand MalCatalog down (they have context access; the libs do not).
foreach ($pg in @('qml/TheatreGenrePage.qml', 'qml/GenrePage.qml')) {
    $src = Get-Content (Join-Path $root $pg) -Raw
    if ($src -notmatch 'MalCatalog') {
        throw "$pg must pass MalCatalog into loadGenre, or the baked catalog never renders"
    }
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

    # Tankoban Discover: when the artifact carries the new schema, the classification
    # table and explicit manga must both be populated. Legacy dbs (pre-rebake) skip.
    $py2 = @"
import sqlite3
d = sqlite3.connect(r'$db')
has = d.execute("SELECT COUNT(1) FROM sqlite_master WHERE type='table' AND name='classification'").fetchone()[0]
if has:
    print(d.execute("SELECT COUNT(1) FROM classification WHERE medium='manga'").fetchone()[0],
          d.execute("SELECT COUNT(1) FROM manga WHERE explicit=1").fetchone()[0])
else:
    print('legacy')
"@
    $dprobe = ($py2 | python -).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'discover probe unreadable' }
    if ($dprobe -ne 'legacy') {
        $dn = [regex]::Matches($dprobe, '\d+') | ForEach-Object { [int]$_.Value }
        if ($dn[0] -le 0) { throw "classification table empty: $dprobe" }
        if ($dn[1] -le 0) { throw "no explicit manga baked: $dprobe" }
        Write-Host "  discover live: $dprobe (manga classifications, explicit manga)"
    }
} else {
    Write-Host '  baked db absent - fallback lane covers (run scripts/anime_brain/build_mal_db.py)'
}

Write-Host 'test_mal_genre_catalog_p0: PASS (catalog-first both lanes, live ladder intact)'
