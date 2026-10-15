# The player HUD adopts Segoe UI (Harbor's effective Windows face) with fixed-width (tabular)
# numerals, and renders transport icons through the reusable Lucide PlayerIcon/PlayerSeekIcon
# components instead of the old hand-drawn Canvas IconGlyph. The seek "10" is centered QML text.
# Approved design: docs/superpowers/specs/2026-07-19-colosseum-harbor-player-polish-design.md

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$theme  = Get-Content (Join-Path $root "qml/Theme.qml") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- the HUD font token exists and resolves to Segoe UI (approved 2026-07-19) ---
Assert-Contains    $theme 'readonly property string hud: "Segoe UI"' "Theme.hud must resolve to Segoe UI."
Assert-NotContains $theme 'readonly property string hud: "Inter"'    "Theme.hud must no longer be Inter."

# --- the player uses the HUD token, not the raw system font, and carries no code font ---
Assert-Contains $player 'theme.hud' "Player text must use the Segoe UI HUD token."
$uiLeft = [regex]::Matches($player, 'theme\.ui').Count
if ($uiLeft -ne 0) { throw "Player must route all HUD text through theme.hud, not theme.ui; found $uiLeft." }
Assert-NotContains $player 'Consolas' "The Consolas code font must be gone from the player."

# --- fixed-width digits on the timestamps ---
Assert-Contains $player 'font.features' "Timestamps must set font.features for tabular (fixed-width) digits."
Assert-Contains $player '"tnum"' "Timestamps must enable the tnum (tabular numerals) feature."

# --- transport icons render through the reusable Lucide components, not Canvas ---
Assert-Contains    $player 'PlayerIcon {'                    "Transport buttons must render the Lucide PlayerIcon component."
Assert-Contains    $player 'PlayerSeekIcon {'                "The +/-10s buttons must render PlayerSeekIcon (rotate glyph + centered numeral)."
Assert-NotContains $player 'component IconGlyph: Canvas'     "The hand-drawn Canvas IconGlyph renderer must be gone from the main chrome."
Assert-NotContains $player 'circleArc(0.27, fwd ? 320 : 220' "The old partial seek arc must be gone."

Write-Host "Player HUD typography contract checks passed."
