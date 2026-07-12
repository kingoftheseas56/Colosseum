$ErrorActionPreference = "Stop"

# Universe Atlas hero contract (spec: haven docs/superpowers/specs/
# 2026-07-12-colosseum-universe-atlas-hero-design.md, mock rev 2 ratified).
# Folio + ledger + spine rail; dots dead; no poster; manga counts never "volumes".

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Lacks($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- the component ---
$ua = Read-File "qml/UniverseAtlas.qml"
foreach ($n in @('signal exploreRequested(string name)', 'Universes.ledger(', 'id: rail',
                 'id: prog', 'SwipeView', 'id: advance')) {
    Assert-Contains $ua $n "UniverseAtlas must carry: $n"
}
Assert-Lacks $ua 'volumes' "Manga counts are DIFFERENT MANGA - the component must never say volumes (ratified)."
Assert-Lacks $ua 'poster' "No hero poster - the banner is the art statement (ratified)."
# exactly ONE Timer: the page-turn and the progress line must share it (no drift)
$timerCount = ([regex]::Matches($ua, 'Timer \{')).Count
if ($timerCount -ne 1) { throw "UniverseAtlas must have exactly ONE Timer (page-turn = progress line), found $timerCount." }

# --- the parse is real ---
$uj = Read-File "qml/Universes.js"
Assert-Contains $uj 'function ledger(' "Universes.js must carry the pure ledger parse."

# --- Main.qml: atlas in, dots dead ---
$main = Read-File "qml/Main.qml"
Assert-Contains $main 'UniverseAtlas {' "Main.qml must instantiate UniverseAtlas."
Assert-Contains $main 'onExploreRequested' "Main.qml must route exploreRequested to openUniverse."
Assert-Lacks $main 'id: heroView' "The old inline hero (heroView) must be gone from Main.qml."
Assert-Lacks $main 'dots -- track the current universe' "The dots block must be gone."
Assert-Lacks $main 'index === heroView.currentIndex ? 22 : 8' "The dot pills must be gone."

Write-Host "universe atlas p0: OK"
