$ErrorActionPreference = "Stop"

# Universe Exhibit hero contract (spec: haven docs/superpowers/specs/
# 2026-07-12-colosseum-universe-exhibit-hero-design.md, mock rev 3 ratified).
# Words on an opaque matte; art in a framed canvas; a PURE carousel:
# no dots, no tabs, no timer bar, no media counts (all ratified out 2026-07-12).

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Lacks($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# the Exhibit is in
Assert-Contains $main 'id: matte' "Hero must carry the opaque matte column (words never sit on art)."
Assert-Contains $main 'id: canvas' "Hero must frame the banner as a canvas."
Assert-Contains $main 'id: heroView' "Hero must keep the SwipeView (native swipe = the only manual nav)."
# NOTE: -like treats [] as a wildcard char class, so the needle avoids the index brackets
Assert-Contains $main 'onClicked: win.openUniverse(Universes.universes' "Explore must still route to openUniverse."

# the retired chrome is OUT
Assert-Lacks $main 'index === heroView.currentIndex ? 22 : 8' "The dot pills must stay dead (pure carousel, ratified)."
Assert-Lacks $main 'modelData.chips' "The hero media-counts chips must stay dead (ratified: details cut)."
Assert-Lacks $main 'Universes.ledger' "The Atlas ledger must not return (design superseded)."

Write-Host "universe exhibit p0: OK"
