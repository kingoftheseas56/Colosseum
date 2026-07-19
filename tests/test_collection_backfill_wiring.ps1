$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Read-File($p) { Get-Content -Raw (Join-Path $root $p) }
function Assert-Contains($hay, $needle, $msg) { if (-not $hay.Contains($needle)) { Write-Host "FAIL: $msg"; exit 1 } }
$bf = Read-File "qml/CollectionBackfill.js"
Assert-Contains $bf "function entryForTheatreSeries" "backfill maps theatre"
Assert-Contains $bf "function entryForTankobanSeries" "backfill maps tankoban"
Assert-Contains $bf "function entryForBook" "backfill maps biblio"
$m = Read-File "qml/Main.qml"
Assert-Contains $m "function runCollectionBackfill(" "Main has the backfill driver"
Assert-Contains $m "runCollectionBackfill()" "backfill is called at startup"
Assert-Contains $m 'Collection.add("_meta"' "backfill sets the run-once marker"
Assert-Contains $m 'CollectionBackfill.entryForTheatreSeries' "driver uses the mapper"
Assert-Contains $bf "function titleKey" "backfill has title dedup helper"
Assert-Contains $m 'backfill_v2' "run-once marker bumped to v2"
Write-Host "test_collection_backfill_wiring PASSED"
