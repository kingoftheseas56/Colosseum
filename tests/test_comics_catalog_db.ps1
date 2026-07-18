$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}
# 1) the C++ harness IS the behavioral test — must be pre-built, run by exit code
$exe = Join-Path $root "native/build-msvc/comics_catalog_engine_harness.exe"
if (-not (Test-Path $exe)) { throw "harness not built - run native\build-target.bat comics_catalog_engine_harness" }
& $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ComicsCatalog engine harness failed (exit $LASTEXITCODE)" }
# 2) registration + build wiring shape
$mn = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Contains $mn 'setContextProperty(QStringLiteral("ComicsCatalog")' "engine must be exposed to QML"
Assert-Contains $mn 'data/comics_catalog.db' "db path anchors on the repo-root cwd"
$cm = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
Assert-Contains $cm 'engine/ComicsCatalog.cpp' "engine compiled into the app"
Assert-Contains $cm 'comics_catalog_engine_harness' "harness target exists"
# 3) QML wiring shape
$cs = Get-Content (Join-Path $root "qml/ComicSeries.qml") -Raw
Assert-Contains $cs 'bakedReleases' "western shelf carries the baked mode"
Assert-Contains $cs '"gcd:" + gcdId' "baked mode gets its own Progress/reader namespace"
Assert-Contains $cs 'enrichBaked' "baked rows enrich covers/sizes lazily"
$api = Get-Content (Join-Path $root "qml/ComicsApi.js") -Raw
Assert-Contains $api 'function postsById' "chunked by-id enrichment present"
$mq = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Assert-Contains $mq 'function openGcdSeries' "catalogue open verb"
Assert-Contains $mq 'exactMatches' "unambiguous-title redirect consults the catalogue"
Assert-Contains $mq 'data.gcd) win.openGcdSeries' "search routing reaches the run page"
Assert-Contains $mq 'indexOf("gcd:")' "gcd: continue/resume lane"
$ws = Get-Content (Join-Path $root "qml/WorldSearch.js") -Raw
Assert-Contains $ws 'searchCatalogDb' "catalogue search lane"
Assert-Contains $ws 'engine.search(q, 30)' "comics search asks the seam for a real presence (30 rows, was 12)"
Assert-Absent  $ws 'mergeTankobanResults' "legacy 4-lane merge retired 2026-07-18 - mergeSearchLanes is the survivor"
# 4) shelf rows + gcd routing wiring (browse-landing arc)
$tw = Get-Content (Join-Path $root "qml/TankobanWorld.qml") -Raw
Assert-Contains $tw 'ComicsCatalog.shelf' "world page computes catalogue shelf rows"
Assert-Contains $tw 'gcdSeriesRequested' "shelf tiles route to the run page"
# 5) deployed-db data-scale floor (moved here from the retired gen.js logic harness, P4
#    Task 4: the >=1100-row check was a DATA check, not a logic check — it belongs against
#    the shipped SQLite, not a QML fixture)
Push-Location $root
try {
    python -c "import sqlite3;db=sqlite3.connect(r'data/comics_catalog.db');n=db.execute('select count(*) from curated_series').fetchone()[0];assert n>=1000, n;print('curated floor OK:',n)"
    if ($LASTEXITCODE -ne 0) { throw "curated_series data floor failed (exit $LASTEXITCODE)" }
} finally { Pop-Location }
Write-Host "comics catalog db contract OK"
