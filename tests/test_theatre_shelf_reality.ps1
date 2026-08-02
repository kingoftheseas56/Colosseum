# The anti-fixture gate (spec §7): every index shelf's REAL titles, mechanically checked,
# full lists saved to tests/_reality_shelves.txt for Hemanth's eyes-on.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "native\build-msvc\colosseum.exe"
if (!(Test-Path $exe)) { throw "build colosseum first" }
if (!(Test-Path (Join-Path $root "data\imdb_catalog.db"))) { throw "bake data/imdb_catalog.db first (Task 1)" }
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_QPA_PLATFORM = "offscreen"
$out = cmd /c "`"$exe`" `"$root\tests\theatre_shelf_reality_probe.qml`" 2>&1" | Out-String
$out | Set-Content (Join-Path $root "tests\_reality_shelves.txt")
if ($out -notlike "*REALITY_PROBE_DONE*") { throw "probe did not complete:`n$($out.Substring(0, [Math]::Min(2000, $out.Length)))" }

function Shelf($key) {
    $line = ($out -split "`n") | Where-Object { $_ -like "*SHELF $key *" } | Select-Object -First 1
    if (-not $line) { throw "shelf $key missing from probe output" }
    return $line
}
function AssertIn($key, $needle)  { if ((Shelf $key) -notlike "*$needle*") { throw "$key must contain $needle" } }
function AssertOut($key, $needle) { if ((Shelf $key) -like "*$needle*")   { throw "$key must NOT contain $needle" } }
function AssertDepth($key, $min) {
    if ([int]((Shelf $key) -replace '.*\((\d+)\):.*', '$1') -lt $min) { throw "$key thinner than $min titles" }
}

# spec §7 title-level truths
AssertIn  "movies/top-rated"    "Shawshank"
AssertOut "movies/hidden-gems"  "Shawshank";  AssertOut "movies/hidden-gems" "Godfather"
AssertOut "movies/hidden-gems"  "Dark Knight"
AssertOut "movies/top-rated"    "ANIME";      AssertOut "movies/animated-movies" "ANIME"
AssertIn  "shows/top-rated"     "Breaking Bad"
AssertOut "shows/top-rated"     "Attack on Titan"
AssertIn  "shows/limited-series" "Chernobyl"
AssertOut "shows/korean-drama"  "X-Men";      AssertOut "shows/korean-drama" "Korra"
AssertOut "shows/korean-drama"  "Avatar";     AssertOut "shows/korean-drama" "Solo Leveling"
# Live-action-only: anime the Fribb set missed (Bleach TYBW) and game-shows must not appear.
AssertOut "shows/korean-drama"  "Bleach";     AssertOut "shows/korean-drama" "Culinary Class Wars"
AssertIn  "shows/korean-drama"  "The Glory"
# NOTE (accepted keyless residual, ratified 2026-08-02): Squid Game / All of Us Are Dead do not
# surface here — a global simultaneous release leaves origLang empty/low, unreachable keyless.
AssertOut "shows/animated-series" "One Piece"
AssertIn  "shows/animated-series" "Simpsons"
AssertIn  "movies/french-cinema" "Am"           # Amélie (accent-safe grep)
foreach ($k in @("movies/top-rated","movies/hidden-gems","movies/cult-classics",
                 "movies/international-cinema","movies/korean-cinema",
                 "shows/top-rated","shows/hidden-gems","shows/cult-classics","shows/korean-drama")) {
    AssertDepth $k 20
}
# cult classics: every year pre-2000
$cc = Shelf "movies/cult-classics"
([regex]::Matches($cc, '\((\d{4})')) | ForEach-Object {
    if ([int]$_.Groups[1].Value -gt 1999) { throw "cult classics leaked a post-1999 year" } }

Write-Host "THEATRE_SHELF_REALITY_OK"
