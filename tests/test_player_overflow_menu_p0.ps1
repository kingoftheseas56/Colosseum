$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Narrow player folds hidden controls into an overflow menu instead of deleting them
# (spec 2026-07-06 slice 3).
Assert-Contains $player 'property bool overflowOpen' `
    "Overflow panel state must exist."
Assert-Contains $player 'icon: "more"' `
    "The dock must show a 'more' (overflow) button."
Assert-Contains $player 'visible: compact' `
    "Overflow button appears exactly when controls start hiding (compact)."
# Hidden menu buttons must reappear while their panel is open (the fold-back path).
Assert-Contains $player 'visible: !tight || audioMenu.panelOpen' `
    "AudioMenu must be reachable from overflow in tight mode."
Assert-Contains $player 'visible: !compact || speedMenu.panelOpen' `
    "Speed must be reachable from overflow in compact mode."
Assert-Contains $player 'visible: !compact || fillMenu.panelOpen' `
    "Picture must be reachable from overflow in compact mode."
Assert-Contains $player 'root.overflowOpen = false' `
    "closeMenus must dismiss the overflow panel."

Write-Host "Player overflow-menu contract checks passed."
