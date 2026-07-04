$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Count-Occurrences($text, $needle) {
    return ([regex]::Matches($text, [regex]::Escape($needle))).Count
}

# Issue 2: a click that only dismisses an open menu must not also toggle play/pause.
Assert-Contains $player "if (root.anyMenuOpen) {" `
    "The background click must branch on anyMenuOpen so dismissing a menu doesn't pause."

# Issue 1: dock popovers were losing clicks on rows rendered above the short dock (Qt bounds
# pointer delivery to the dock ancestor). They must be reparented to the full-screen chrome.
$reparents = Count-Occurrences $player "parent: chrome"
if ($reparents -lt 3) {
    throw "Fill / Speed / Tools popovers must be reparented to chrome (found $reparents of >=3)."
}
Assert-Contains $player "fm.mapToItem(chrome, 0, 0)" `
    "The fill popover must position itself in chrome coordinates."
Assert-Contains $player "sm.mapToItem(chrome, 0, 0)" `
    "The speed popover must position itself in chrome coordinates."
Assert-Contains $player "tm.mapToItem(chrome, 0, 0)" `
    "The tools popover must position itself in chrome coordinates."

Write-Host "Player menu click fixes contract checks passed."
