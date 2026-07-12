$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Assert-Contains($hay, $needle, $why) {
    if ($hay -notlike "*$needle*") { throw "MISSING: $needle -- $why" }
}
$cpp = Get-Content (Join-Path $root "native/engine/MangaDownloader.cpp") -Raw
Assert-Contains $cpp 'pruning ghost entry' "loadIndex must drop entries whose dir is missing (self-heal 2026-07-12)"
Assert-Contains $cpp 'repaired ledger path' "self-heal must repair re-rooted paths before ever pruning (falsified-premise fix 2026-07-12)"
Assert-Contains $cpp 'INDEX-SELFTEST' "self-heal must have a runnable proof lane"
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
Assert-Contains $main 'COLOSSEUM_INDEX_SELFTEST' "selftest lane must be reachable from env"
Write-Host "index self-heal P0 contract OK"
