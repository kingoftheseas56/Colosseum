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

# 5) the COMICS column (Hemanth 2026-07-12: Avatar gets the GC archive in the era gallery):
#    curated pin → loadEras pass-through → era template door → Main routes to the archive index
Assert-Contains $udb 'comics: { tag: "avatar-the-last-airbender", tagId: 448' "Avatar must pin its GC archive (slug + tagId)."
$saga = Read-File "qml/SagaApi.js"
Assert-Contains $saga 'comics: cfg.comics || null' "loadEras must pass the curated comics pin through."
$era = Read-File "qml/EraUniversePage.qml"
foreach ($n in @('signal comicsArchiveRequested(var box)', 'function comicsDoor()',
                 'ComicsApi.tagBox', 'GETCOMICS ARCHIVE')) {
    Assert-Contains $era $n "EraUniversePage must carry the comics column: $n"
}
$capi = Read-File "qml/ComicsApi.js"
Assert-Contains $capi 'function tagBox(' "ComicsApi must resolve a pinned tag into an explore-box shape."
$main = Read-File "qml/Main.qml"
Assert-Contains $main 'item.comicsArchiveRequested.connect(win.openComicArchive)' "Main must route the comics column to the archive index."

# 6) UPCOMING law (Hemanth 2026-07-13): metadata id = the gate, never release dates —
#    future work stays in its room wearing the small tag
Assert-Contains $saga 'upcoming:' "mapWatch must carry the upcoming flag."
Assert-Contains $saga '_upcomingById' "the cached same-year boundary probe must exist."
Assert-Contains $saga 'function probeUpcoming(' "the boundary probe must be the shared helper."
Assert-Contains $era 'UPCOMING' "EraUniversePage must render the UPCOMING tag."
$sagaPage = Read-File "qml/SagaUniversePage.qml"
Assert-Contains $sagaPage 'UPCOMING' "SagaUniversePage must render the UPCOMING tag (HP show case)."

# 7) the 2026-07-13 expansion sweep (report-driven, every pin agent-verified live):
#    id-pins for same-name traps, comics doors on saga/galaxy, books on eras
foreach ($n in @('tt27497448',          # A Knight of the Seven Kingdoms
                 'tt13918446',          # the HBO Harry Potter show (UPCOMING)
                 'tt9603060',           # Star Trek: Section 31
                 'tt8622160',           # Starfleet Academy
                 'tt0361243',           # Clone Wars 2003 (2008 namesake outranks it)
                 'tt0087182',           # Dune 1984
                 'tt28283547',          # The Rats: A Witcher Tale
                 'comics: { tag: "james-bond", tagId: 2111',
                 'comics: { tag: "star-trek", tagId: 691',
                 'comics: { tag: "star-wars", tagId: 203',
                 'novelsTitle: "The Fleming Shelf"',
                 'novelsTitle: "Chronicles of the Avatar"',
                 'Crossroads of Ravens',
                 'Boruto: Two Blue Vortex')) {
    Assert-Contains $udb $n "Universes.js must carry the expansion pin: $n"
}
$galaxyPage = Read-File "qml/GalaxyUniversePage.qml"
Assert-Contains $galaxyPage 'comicsArchiveRequested' "GalaxyUniversePage must carry the comics door."
Assert-Contains $sagaPage 'comicsArchiveRequested' "SagaUniversePage must carry the comics door."
Assert-Contains $era 'signal bookRequested(var book)' "EraUniversePage must carry the books shelf verb."

Write-Host "universe expansion p0: OK"
