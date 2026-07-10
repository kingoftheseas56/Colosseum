$ErrorActionPreference = "Stop"

# Continue see-all contract (spec: haven docs/superpowers/specs/
# 2026-07-11-colosseum-continue-see-all-design.md). One shared page, four doors;
# chips exclusive; Most Watched ABSENT (no store counter yet); ProgressStore untouched.

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

# --- the page ---
$pg = Read-File "qml/ContinueSeeAllPage.qml"
foreach ($n in @('property string scope', 'signal resumeRequested(var item)', 'signal detailRequested(var item)',
                 'ContinueSeeAll.js', 'Progress.recent("", 0)', 'Progress.recent("manga", 0).concat(Progress.recent("comic", 0))',
                 'Progress.revision', 'ContinueTile {', 'variant: "world"', 'Progress.forget',
                 'Nothing to continue.', 'Nothing here yet.', 'HouseScrollBar')) {
    Assert-Contains $pg $n "ContinueSeeAllPage must carry: $n"
}
Assert-Lacks $pg 'label: "Most Watched"' "Most Watched chip must NOT ship without a real watch counter (ratified)."

# --- the logic module is pure ---
$js = Read-File "qml/ContinueSeeAll.js"
Assert-Contains $js '.pragma library' "ContinueSeeAll.js must be a pure library."
Assert-Lacks $js '.import' "ContinueSeeAll.js must stay dependency-free (headless-testable)."

# --- the row header door ---
$cr = Read-File "qml/ContinueRow.qml"
Assert-Contains $cr 'signal seeAllRequested()' "ContinueRow must emit seeAllRequested."
Assert-Contains $cr 'moreLabel: "See all"' "ContinueRow header must label the chevron 'See all'."
Assert-Lacks $cr 'navigable: false' "ContinueRow's chevron must be ON - the see-all page exists now."

# --- the world relays (signal declared ONCE on the WorldPage base — QML forbids
#     re-declaring a superclass signal in the derived worlds) ---
$wp = Read-File "qml/WorldPage.qml"
Assert-Contains $wp 'signal continueSeeAllRequested()' "WorldPage base must declare continueSeeAllRequested."
foreach ($w in @("qml/TheatreWorld.qml", "qml/TankobanWorld.qml", "qml/BiblioWorld.qml")) {
    $t = Read-File $w
    Assert-Contains $t 'onSeeAllRequested' "$w must forward the row's seeAllRequested."
    Assert-Lacks $t 'signal continueSeeAllRequested()' "$w must NOT re-declare the base signal (QML duplicate-signal error)."
}

# --- Main.qml: layer, functions, home door, Esc, world connects ---
$main = Read-File "qml/Main.qml"
foreach ($n in @('id: continueSeeAllLayer', 'ContinueSeeAllPage.qml',
                 'function openContinueSeeAll(', 'function closeContinueSeeAll()',
                 'continueSeeAllLayer.active) win.closeContinueSeeAll()',
                 'win.openContinueSeeAll("home")',
                 'item.continueSeeAllRequested')) {
    Assert-Contains $main $n "Main.qml must wire: $n"
}

Write-Host "continue see-all p0: OK"
