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
# 2026-10-05 needle refresh (Arc 58): the baked Universes.js roster is gone (the shell hands
# the installed roster down), the bar moved to UniverseLedgerBar.qml (2026-10-04 extraction),
# and the wiring needles follow the current Main.qml shapes. The hall itself has NO product
# door since Arc 58 (Home's tribute link and Extensions open the Universes WORLD via
# win.openWorld("Universes")); openUniverseHall() is orphaned pending the reachability audit,
# so this check guards the hall's FORM + layer wiring for as long as it stays in source.
$hp = Read-File "qml/UniverseHallPage.qml"
foreach ($n in @('signal exploreRequested(string extensionId, string name)', 'property int hovered',
                 'property var universes:',
                 'Hall of Tributes',
                 'flickableDirection: Flickable.VerticalFlick',
                 'ScrollBar.vertical: HouseScrollBar', 'ScrollGlide')) {
    Assert-Contains $hp $n "UniverseHallPage must carry: $n"
}
# The bar itself (breathe height Behaviour + the washed-dark horizontal gradient) moved to
# UniverseLedgerBar.qml in the 2026-10-04 extraction — the Hall still houses the pile, the
# bar's own checks point at the component now.
$lb = Read-File "qml/UniverseLedgerBar.qml"
foreach ($n in @('Behavior on height', 'Gradient.Horizontal')) {
    Assert-Contains $lb $n "UniverseLedgerBar must carry: $n"
}
Assert-Lacks $hp 'PortraitTile' "The hall must NOT be generic tiles (ratified constraint)."
Assert-Lacks $hp 'ContinueTile' "The hall must NOT reuse the continue tiles."
Assert-Lacks $hp 'GridView' "The hall is a pile of bars, not a grid."
Assert-Lacks $hp 'rotation: -90' "Names read LEVEL in the ledger stack - no rotated spine type."
Assert-Lacks $hp 'HorizontalFlick' "The sideways walk is dead - the pile scrolls down only."
# the breathe must go still while the pile moves (2026-07-13: bars ballooning under the
# cursor mid-scroll made scrolling a nightmare)
Assert-Contains $hp 'property bool walking' "The hall must hold the breathe still while scrolling."
Assert-Contains $hp 'if (!root.walking && !hallNav.keyboardRecentlyMoved) root.hovered' "Hover must be gated on the walk settling (and on keyboard motion settling)."

# the PROPER THUMB law (Hemanth 2026-07-13, reversing the sliver): the house scrollbar is
# always present when the page overflows, and grabbable — never a motion-revealed phantom
$sb = Read-File "qml/HouseScrollBar.qml"
Assert-Contains $sb 'interactive: true' "HouseScrollBar must be grabbable (the proper thumb, ratified)."
Assert-Contains $sb 'ScrollBar.AlwaysOn' "HouseScrollBar must stand always-on when content overflows."
Assert-Lacks $sb 'flick.moving || flick.flicking' "The motion-revealed sliver machinery must not return."

# --- wiring: layer under the universe pages, back door, Esc chain ---
# 2026-10-05: the hero "Explore the tribute" door and the Extensions tile now open the
# Universes WORLD (win.openWorld("Universes")), not the hall — openUniverseHall() has no
# caller. closeUniverse() no longer falls back through the hall (it just deactivates
# universeLayer; the Universes world owns the surface beneath). Both retired needles are
# recorded here so the change is auditable; what remains guarded is the hall's own layer
# wiring while the page stays in source.
$main = Read-File "qml/Main.qml"
foreach ($n in @('id: universeHallLayer', 'UniverseHallPage.qml',
                 'function openUniverseHall()', 'function closeUniverseHall()',
                 'case "universeHall": win.closeUniverseHall(); return',
                 'item.universes = win.installedUniverses',
                 'item.backRequested.connect(win.closeUniverseHall)',
                 'onClicked: win.openWorld("Universes")')) {
    Assert-Contains $main $n "Main.qml must wire: $n"
}

Write-Host "universe hall p0: OK"
