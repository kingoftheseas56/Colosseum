# The main player transport renders through a vendored Lucide SVG subset (lucide-static@0.460.0,
# Harbor's pinned version) via reusable PlayerIcon / PlayerSeekIcon components — not the old
# hand-drawn Canvas IconGlyph. Provenance + license are captured in-repo; assets are never
# fetched at app runtime. The seek "10" is centered QML Text, so the seek SVGs carry no <text>.
# docs/superpowers/specs/2026-07-19-colosseum-harbor-player-polish-design.md

$ErrorActionPreference = "Stop"

$root      = Split-Path -Parent $PSScriptRoot
$iconsDir  = Join-Path $root "assets/icons/lucide"
$vendor    = Join-Path $root "scripts/vendor_lucide_player_icons.ps1"
$playerIcon= Join-Path $root "qml/PlayerIcon.qml"
$seekIcon  = Join-Path $root "qml/PlayerSeekIcon.qml"
$player    = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-File($path, $message) {
    if (-not (Test-Path $path)) { throw $message }
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- deterministic vendoring script + provenance + license ---
Assert-File $vendor "The Lucide vendoring script must exist (scripts/vendor_lucide_player_icons.ps1)."
$vendorTxt = Get-Content $vendor -Raw
Assert-Contains $vendorTxt 'lucide-static@0.460.0' "Vendoring must pin lucide-static@0.460.0 (Harbor parity)."
Assert-Contains $vendorTxt 'npm pack'              "Vendoring must use 'npm pack' (no runtime fetch)."

Assert-File (Join-Path $iconsDir "LICENSE")    "The upstream Lucide ISC LICENSE must be vendored."
Assert-Contains (Get-Content (Join-Path $iconsDir "LICENSE") -Raw) 'ISC' "The vendored LICENSE must be the ISC license."
Assert-File (Join-Path $iconsDir "SOURCE.txt") "SOURCE.txt must record package/version/integrity provenance."
Assert-Contains (Get-Content (Join-Path $iconsDir "SOURCE.txt") -Raw) '0.460.0' "SOURCE.txt must record the pinned version."

# --- every mapped SVG present (the actual used-set, not the full Lucide library) ---
$svgs = @(
  'arrow-left','rotate-ccw','rotate-cw','skip-back','skip-forward','play','pause',
  'maximize','minimize','minus','x','volume-2','volume-x','audio-lines','captions',
  'gallery-horizontal-end','scan','circle-alert'
)
foreach ($name in $svgs) {
    Assert-File (Join-Path $iconsDir "$name.svg") "Mapped Lucide asset missing: $name.svg"
}

# --- the seek glyphs carry NO baked SVG text; the numeral is QML Text ---
foreach ($seek in @('rotate-ccw','rotate-cw')) {
    Assert-NotContains (Get-Content (Join-Path $iconsDir "$seek.svg") -Raw) '<text' "Seek glyph $seek.svg must not bake numerals as SVG <text>."
}

# --- reusable components exist with the public API the player wires to ---
Assert-File $playerIcon "qml/PlayerIcon.qml must exist."
$piTxt = Get-Content $playerIcon -Raw
Assert-Contains $piTxt 'property string kind'  "PlayerIcon must expose a 'kind' property."
Assert-Contains $piTxt 'MultiEffect'           "PlayerIcon must tint the SVG via MultiEffect (colorization)."
Assert-File $seekIcon "qml/PlayerSeekIcon.qml must exist."
$siTxt = Get-Content $seekIcon -Raw
Assert-Contains $siTxt 'PlayerIcon'            "PlayerSeekIcon must compose PlayerIcon (rotate glyph)."
Assert-Contains $siTxt 'Text'                  "PlayerSeekIcon must render the numeral as centered QML Text."

# --- the player renders through the new components and the Canvas glyph renderer is gone ---
Assert-Contains    $player 'PlayerIcon {'                "PlayerPage must render transport buttons via PlayerIcon."
Assert-Contains    $player 'PlayerSeekIcon {'            "PlayerPage must render the +/-10s buttons via PlayerSeekIcon."
Assert-NotContains $player 'component IconGlyph: Canvas' "The hand-drawn Canvas IconGlyph renderer must be removed."

Write-Host "Player Lucide icon contract checks passed."
