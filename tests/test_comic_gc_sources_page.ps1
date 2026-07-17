$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
function Assert-Absent($hay, $needle, $why) {
    if ($hay -like "*$needle*") { throw "STALE: $needle -- $why" }
}
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (-not (Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe" }
$harness = Join-Path $root "tests/comic_gc_sources_logic_harness.qml"
# cmd /c + QT_FORCE_STDERR_LOGGING: PowerShell's native `&`/2>&1 redirection does not
# reliably capture qml.exe's (GUI-subsystem) stdout on this machine -- same workaround
# already proven in tests/test_comics_catalog_v1.ps1.
$env:QT_FORCE_STDERR_LOGGING = "1"
$output = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($LASTEXITCODE -ne 0) { throw "GC sources logic harness failed (exit $LASTEXITCODE):`n$output" }
if ($output -notlike "*GC-SOURCES-LOGIC OK*") { throw "harness ran but verdict line missing:`n$output" }
# -- enrichment glue: one WP call for the attached ids, parse single-sourced in mapPosts --
$api = Get-Content (Join-Path $root "qml/ComicsApi.js") -Raw
Assert-Contains $api 'function postsById' "sources page enrichment entry point"
Assert-Contains $api 'include=' "by-id fetch must ride WP's include param (one request, fold caps at 20 ids)"
# -- the page: house shell + moved-in download state machine --
$pg = Get-Content (Join-Path $root "qml/ComicGcSourcesPage.qml") -Raw
Assert-Contains $pg '"gcpost-"' "download identity must stay in the rail's namespace - history carries over"
Assert-Contains $pg 'failureIsTerminal' "dead posts are terminal: grayed, no retry (rail rule moves over)"
Assert-Contains $pg 'readRequested' "downloaded post must open the reader via the series page's path"
Assert-Contains $pg 'ComicGcSources.js' "grouping/sort must ride the tested pure-logic module"
Assert-Contains $pg 'postsById' "covers+sizes arrive via the one-call enrichment"
Assert-Contains $pg 'ALSO ON GETCOMICS' "hero eyebrow"
# -- ledger: rail retired, doorway banner in (mock option C, universe-door sibling) --
$led = Get-Content (Join-Path $root "qml/ComicDbLedger.qml") -Raw
Assert-Contains $led 'sourcesPageRequested' "banner must emit the page-open signal"
Assert-Contains $led 'ALSO ON GETCOMICS' "doorway banner eyebrow"
Assert-Absent $led '"gcpost-"' "download state machine moved to ComicGcSourcesPage - none may remain in the ledger"
Assert-Absent $led 'statusLine' "rail row machinery must be fully removed, not hidden"
# -- wiring: ledger -> series page -> Main layer over the series page --
$sp = Get-Content (Join-Path $root "qml/ComicSeriesPage.qml") -Raw
Assert-Contains $sp 'gcSourcesRequested' "series page must re-emit the banner click with the series payload"
$mn = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Assert-Contains $mn 'gcSourcesLayer' "the page needs its Loader layer"
Assert-Contains $mn 'openGcSources' "open verb"
Assert-Contains $mn 'closeGcSources' "close verb (back + Esc)"
Assert-Contains $mn 'gcSourcesLayer.active) win.closeGcSources()' "Esc must close the sources page BEFORE the series page under it"
Write-Host "comic gc sources page contract OK"
