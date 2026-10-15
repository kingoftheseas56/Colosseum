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
Assert-Contains $player 'visible: true   // the four real tools always live here' `
    "Overflow button appears exactly when controls start hiding (compact)."
# Hidden menu buttons must reappear while their panel is open (the fold-back path).
# Audio & speed chips fold at barTiny (native chrome 2026-07-08 - chips hold to tiny);
# Picture left the bar entirely and lives only in overflow (its button appears only while
# its own popover is open).
Assert-Contains $player 'visible: !root.barTiny || audioMenu.panelOpen' `
    "AudioMenu must be reachable from overflow in tight mode."
Assert-Contains $player 'visible: !root.barTiny || speedMenu.panelOpen' `
    "Speed must be reachable from overflow when tiny."
Assert-Contains $player 'visible: fillMenu.panelOpen' `
    "Picture lives in overflow - its bar button shows only while its popover is open."
Assert-Contains $player '"label": "Picture", "kind": "fill", "when": true' `
    "Picture must always be an overflow row now."
Assert-Contains $player 'root.overflowOpen = false' `
    "closeMenus must dismiss the overflow panel."

Write-Host "Player overflow-menu contract checks passed."
