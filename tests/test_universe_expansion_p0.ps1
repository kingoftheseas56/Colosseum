$ErrorActionPreference = "Stop"

# Universe expansion contract (Hemanth commission 2026-07-12): 12 curated universes, every
# page real. Pure logic + .import parse proven by the merge harness (exit-code verdict);
# this script runs it, then greps the shape needles.

$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# 1) behavioral: mergeById + configFor + the 12-universe collection audit
$harness = Join-Path $PSScriptRoot "universe_api_merge_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "universe merge/config harness failed (exit $LASTEXITCODE)" }

function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# 2) shape: the API rides the ONE curation point
$api = Read-File "qml/UniverseApi.js"
Assert-Contains $api '.import "Universes.js" as UDB' "UniverseApi must import the curation point."
Assert-Contains $api 'function mergeById(' "UniverseApi must carry the pure multi-query merge."
Assert-Contains $api 'seriesQueries' "UniverseApi must honor curated series queries."

# 3) shape: the curated universes Hemanth commissioned are present
$udb = Read-File "qml/Universes.js"
foreach ($n in @('Harry Potter', 'Lord of the Rings', 'A Song of Ice and Fire', 'Dragon Ball',
                 'Naruto', 'DC Animated Universe', 'Weekly Shonen Jump', 'Star Trek',
                 'Star Wars', 'Dune', 'function configFor(')) {
    Assert-Contains $udb $n "Universes.js must carry: $n"
}

# 4) the wikimedia banner host is IPv4-pinned (dead-IPv6 machine law)
$cpp = Read-File "native/main.cpp"
Assert-Contains $cpp 'upload.wikimedia.org' "main.cpp must pin upload.wikimedia.org (WSJ banner host)."

Write-Host "universe expansion p0: OK"
