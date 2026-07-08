# The player HUD adopts a crafted typeface (Switzer, Harbor-parity) with fixed-width
# (tabular) numerals, and the code-editor font (Consolas) is gone. The ±10s skip glyph
# is a full circular arrow, not the old too-partial squiggle.
# Hemanth 2026-07-07: "apply switzer for everything inside the video player" + the
# 10s buttons must not repeat Harbor's-reference error.

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$theme  = Get-Content (Join-Path $root "qml/Theme.qml") -Raw
$main   = Get-Content (Join-Path $root "qml/Main.qml") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- the HUD font token exists and is Switzer ---
Assert-Contains $theme 'property string hud' "Theme must expose a 'hud' font token for the player."
Assert-Contains $theme '"Inter"' "The player HUD token must be Inter (Hemanth''s flip, 2026-07-08)."

# --- Switzer is bundled + loaded like Fraunces ---
Assert-Contains $main 'Switzer-Regular.otf' "Main must FontLoader the bundled Switzer weights."

# --- the player uses the HUD token, not the raw system font, and no code font ---
Assert-Contains $player 'theme.hud' "Player text must use the Switzer HUD token."
$uiLeft = [regex]::Matches($player, 'theme\.ui').Count
if ($uiLeft -ne 0) { throw "Player must not use theme.ui (Segoe UI) anymore; found $uiLeft." }
Assert-NotContains $player 'Consolas' "The Consolas code font must be gone from the player (the '10' and any numerals)."

# --- fixed-width digits on the timestamps ---
Assert-Contains $player 'font.features' "Timestamps must set font.features for tabular (fixed-width) digits."
Assert-Contains $player '"tnum"' "Timestamps must enable the tnum (tabular numerals) feature."

# --- the seek "10" rides the HUD font (Canvas), not Consolas ---
Assert-Contains $player 'px " + theme.hud' "The seek/skip Canvas numerals must render in the HUD font."

# --- the ±10s glyph is a fuller circular arrow: the old too-partial arc params are gone ---
Assert-NotContains $player 'circleArc(0.27, fwd ? 320 : 220' "The old partial seek arc (the squiggle) must be replaced by a full circular arrow."

Write-Host "Player HUD typography contract checks passed."
