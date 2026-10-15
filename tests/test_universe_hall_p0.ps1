$ErrorActionPreference = "Stop"

# Hall of Worlds contract, second form (Hemanth ratification 2026-07-12 evening): the universe
# see-all is a LEDGER STACK — the books lie flat. Full-width bars, one under another; names
# read LEVEL (no rotated type); the pile scrolls DOWN with the house gold sliver + ScrollGlide;
# hover breathes a bar taller; click enters the world; still never a tile grid. The hall sits
# UNDER the universe layer so entering paints over it and back returns.

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

# --- the hall: flat bars in a vertical pile, breathing on hover ---
$hp = Read-File "qml/UniverseHallPage.qml"
foreach ($n in @('signal exploreRequested(string name)', 'property int hovered',
                 'Universes.universes', 'Hall of Worlds',
                 'flickableDirection: Flickable.VerticalFlick',
                 'ScrollBar.vertical: HouseScrollBar', 'ScrollGlide',
                 'Behavior on height', 'Gradient.Horizontal')) {
    Assert-Contains $hp $n "UniverseHallPage must carry: $n"
}
Assert-Lacks $hp 'PortraitTile' "The hall must NOT be generic tiles (ratified constraint)."
Assert-Lacks $hp 'ContinueTile' "The hall must NOT reuse the continue tiles."
Assert-Lacks $hp 'GridView' "The hall is a pile of bars, not a grid."
Assert-Lacks $hp 'rotation: -90' "Names read LEVEL in the ledger stack - no rotated spine type."
Assert-Lacks $hp 'HorizontalFlick' "The sideways walk is dead - the pile scrolls down only."
# the breathe must go still while the pile moves (2026-07-13: bars ballooning under the
# cursor mid-scroll made scrolling a nightmare)
Assert-Contains $hp 'property bool walking' "The hall must hold the breathe still while scrolling."
Assert-Contains $hp 'if (!root.walking) root.hovered' "Hover must be gated on the walk settling."

# the PROPER THUMB law (Hemanth 2026-07-13, reversing the sliver): the house scrollbar is
# always present when the page overflows, and grabbable — never a motion-revealed phantom
$sb = Read-File "qml/HouseScrollBar.qml"
Assert-Contains $sb 'interactive: true' "HouseScrollBar must be grabbable (the proper thumb, ratified)."
Assert-Contains $sb 'ScrollBar.AlwaysOn' "HouseScrollBar must stand always-on when content overflows."
Assert-Lacks $sb 'flick.moving || flick.flicking' "The motion-revealed sliver machinery must not return."

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
