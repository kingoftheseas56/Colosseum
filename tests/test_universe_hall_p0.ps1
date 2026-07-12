$ErrorActionPreference = "Stop"

# Hall of Worlds contract (Hemanth commission 2026-07-12, free-reign design): the universe
# see-all is a SPINE SHELF, not a tile grid. Hover breathes a spine open; click enters the
# world; the hall sits UNDER the universe layer so entering paints over it and back returns.

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

# --- the hall: spines, not tiles ---
$hp = Read-File "qml/UniverseHallPage.qml"
foreach ($n in @('signal exploreRequested(string name)', 'rotation: -90', 'property int hovered',
                 'Universes.universes', 'Behavior on width', 'Hall of Worlds')) {
    Assert-Contains $hp $n "UniverseHallPage must carry: $n"
}
Assert-Lacks $hp 'PortraitTile' "The hall must NOT be generic tiles (ratified constraint)."
Assert-Lacks $hp 'ContinueTile' "The hall must NOT reuse the continue tiles."
Assert-Lacks $hp 'GridView' "The hall is a shelf, not a grid."

# --- wiring: layer under the universe pages, door on the hero, Esc chain ---
$main = Read-File "qml/Main.qml"
foreach ($n in @('id: universeHallLayer', 'UniverseHallPage.qml',
                 'function openUniverseHall()', 'function closeUniverseHall()',
                 'universeHallLayer.active) win.closeUniverseHall()',
                 'onClicked: win.openUniverseHall()')) {
    Assert-Contains $main $n "Main.qml must wire: $n"
}
# back from a world must land on the hall when it sits beneath
Assert-Contains $main 'if (!universeHallLayer.active) { topbar.visible = true; page.visible = true }' "closeUniverse must fall back to the hall when it is beneath."

Write-Host "universe hall p0: OK"
