$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Width-honest control bar (Hemanth 2026-07-08, "icon vomit after buffering"): the
# right-anchored utility cluster and the centered transport cluster shared one strip
# with nobody checking fit — every added button (stream/download/browser) slid the
# utilities further under the transport. The bar must fold utilities into the overflow
# panel from LIVE geometry, not magic width thresholds.
Assert-Contains $player "readonly property real utilitySpace" `
    "The bar must compute the real space available to the utility cluster."
Assert-Contains $player "transportRow.width" `
    "utilitySpace must be derived from the LIVE transport width (prev/next change it)."
Assert-Contains $player "readonly property bool barSnug" `
    "Tier A/B fold signal (speed/fill/stream/download/browser) must exist."
Assert-Contains $player "readonly property bool barTiny" `
    "Tier C fold signal (audio/tools) must exist."

# The folded buttons must reappear as overflow rows, not vanish.
Assert-Contains $player '"kind": "stream", "when": root.barSnug' `
    "Pick-another-stream must fold into the overflow panel when snug."
Assert-Contains $player '"kind": "download", "when": root.barSnug' `
    "Download must fold into the overflow panel when snug."
Assert-Contains $player '"kind": "browser", "when": root.barSnug' `
    "Episodes & sources must fold into the overflow panel when snug."
Assert-Contains $player 'else if (kind === "browser")' `
    "The overflow row must actually open the drawer."

Write-Host "Player bar-fold contract checks passed."
