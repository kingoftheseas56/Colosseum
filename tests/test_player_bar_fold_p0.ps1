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
# barSnug's only live consumers are the LEFT utility buttons (stream/download); the chip
# roster holds out until barTiny. The old 470 threshold was sized for that chip roster,
# so it folded the left row ~120px too early — context hydration lit prev/next up on the
# Continue-Watching door (2026-07-12) and the wider transport pushed a fullscreen 150%-DPI
# window under 470, hiding change-stream + download for no layout reason. The left row's
# true need is 298px steady / 352 with the transient retry button -> 360.
Assert-Contains $player "readonly property bool barSnug: utilitySpace < 360" `
    "barSnug must be sized to the LEFT utility row's real need (360), not the chip roster's."
Assert-Contains $player "readonly property bool barTiny" `
    "Tier C fold signal (audio/tools) must exist."

# The folded buttons must reappear as overflow rows, not vanish.
Assert-Contains $player '"kind": "stream", "when": root.barSnug' `
    "Pick-another-stream must fold into the overflow panel when snug."
Assert-Contains $player '"kind": "download", "when": root.barSnug' `
    "Download must fold into the overflow panel when snug."
Assert-Contains $player '"kind": "browser", "when": root.barTiny' `
    "Episodes & sources must fold into the overflow panel when snug."
Assert-Contains $player 'else if (kind === "browser")' `
    "The overflow row must actually open the drawer."

Write-Host "Player bar-fold contract checks passed."
