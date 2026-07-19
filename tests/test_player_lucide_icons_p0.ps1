# Semantic icon contract (audit 2026-07-19). Every visible player action renders through the
# vendored Lucide subset (lucide-static@0.460.0) via PlayerIcon/PlayerSeekIcon, mapped to its TRUE
# meaning — and the old July-8 PanelChip text-pill system (EPISODES/AUDIO/SUBTITLES/SPEED) is gone.
# This locks the loophole that let a partial pass look like parity: it checks action->icon MEANING
# and forbids visible text chips, not just that Lucide files exist.

$ErrorActionPreference = "Stop"

$root      = Split-Path -Parent $PSScriptRoot
$iconsDir  = Join-Path $root "assets/icons/lucide"
$vendor    = Join-Path $root "scripts/vendor_lucide_player_icons.ps1"
$playerIcon= Join-Path $root "qml/PlayerIcon.qml"
$seekIcon  = Join-Path $root "qml/PlayerSeekIcon.qml"
$player    = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-File($path, $message) { if (-not (Test-Path $path)) { throw $message } }
function Assert-Contains($text, $needle, $message) { if ($text -notlike "*$needle*") { throw $message } }
function Assert-NotContains($text, $needle, $message) { if ($text -like "*$needle*") { throw $message } }
function Assert-Matches($text, $pattern, $message) { if ($text -notmatch $pattern) { throw $message } }

# --- deterministic vendoring script + provenance + license ---
Assert-File $vendor "The Lucide vendoring script must exist (scripts/vendor_lucide_player_icons.ps1)."
$vendorTxt = Get-Content $vendor -Raw
Assert-Contains $vendorTxt 'lucide-static@0.460.0' "Vendoring must pin lucide-static@0.460.0."
Assert-Contains $vendorTxt 'npm pack'              "Vendoring must use 'npm pack' (no runtime fetch)."
Assert-File (Join-Path $iconsDir "LICENSE")    "The upstream Lucide ISC LICENSE must be vendored."
Assert-Contains (Get-Content (Join-Path $iconsDir "LICENSE") -Raw) 'ISC' "The vendored LICENSE must be the ISC license."
Assert-File (Join-Path $iconsDir "SOURCE.txt") "SOURCE.txt must record provenance."
Assert-Contains (Get-Content (Join-Path $iconsDir "SOURCE.txt") -Raw) '0.460.0' "SOURCE.txt must record the pinned version."

# --- every mapped SVG present; the first (wrong) pass's glyphs are pruned ---
$svgs = @(
  'arrow-left','rotate-ccw','rotate-cw','skip-back','skip-forward','play','pause',
  'maximize','minimize','minus','x','volume-2','volume-x',
  'replace','download','circle-check','triangle-alert','sliders-horizontal',
  'list-video','languages','captions','gauge','circle-alert'
)
foreach ($name in $svgs) {
    Assert-File (Join-Path $iconsDir "$name.svg") "Mapped Lucide asset missing: $name.svg"
}
foreach ($stale in @('gallery-horizontal-end','scan','audio-lines')) {
    if (Test-Path (Join-Path $iconsDir "$stale.svg")) { throw "Stale Lucide asset must be pruned: $stale.svg" }
}

# --- seek glyphs carry NO baked SVG text; vendored SVGs are WHITE (so the tint stays visible) ---
foreach ($seek in @('rotate-ccw','rotate-cw')) {
    Assert-NotContains (Get-Content (Join-Path $iconsDir "$seek.svg") -Raw) '<text' "Seek glyph $seek.svg must not bake numerals as SVG <text>."
}
Assert-Contains (Get-Content (Join-Path $iconsDir "play.svg") -Raw) '#ffffff' "Vendored SVGs must be recolored white for MultiEffect colorization."

# --- reusable components + their public API ---
Assert-File $playerIcon "qml/PlayerIcon.qml must exist."
$piTxt = Get-Content $playerIcon -Raw
Assert-Contains $piTxt 'property string kind'  "PlayerIcon must expose a 'kind' property."
Assert-Contains $piTxt 'MultiEffect'           "PlayerIcon must tint the SVG via MultiEffect (colorization)."
Assert-File $seekIcon "qml/PlayerSeekIcon.qml must exist."
$siTxt = Get-Content $seekIcon -Raw
Assert-Contains $siTxt 'PlayerIcon'            "PlayerSeekIcon must compose PlayerIcon (rotate glyph)."
Assert-Contains $siTxt 'Text'                  "PlayerSeekIcon must render the numeral as centered QML Text."

# --- SEMANTIC action -> icon mappings (the audit's core: right meaning, nothing unmapped) ---
$semanticMap = @(
  @('stream','replace'), @('fit','sliders-horizontal'), @('audio','languages'),
  @('download','download'), @('check','circle-check'), @('warning','triangle-alert'),
  @('speed','gauge'), @('subtitle','captions'), @('episodes','list-video')
)
foreach ($m in $semanticMap) {
    Assert-Matches $piTxt ('case "' + $m[0] + '":\s*return "' + $m[1] + '"') "PlayerIcon must map '$($m[0])' -> '$($m[1])' (semantic audit)."
}

# --- player renders through the new components; the Canvas glyph renderer is gone ---
Assert-Contains    $player 'PlayerIcon {'                "PlayerPage must render transport buttons via PlayerIcon."
Assert-Contains    $player 'PlayerSeekIcon {'            "PlayerPage must render the +/-10s buttons via PlayerSeekIcon."
Assert-NotContains $player 'component IconGlyph: Canvas' "The hand-drawn Canvas IconGlyph renderer must be removed."

# --- the OLD text-chip system is GONE: no PanelChip, no toolbar text pills, no value-on-face ---
Assert-NotContains $player 'component PanelChip'      "The PanelChip text-pill component must be removed."
Assert-NotContains $player 'label: "EPISODES"'        "Episodes must be a list-video icon, not an EPISODES text pill."
Assert-NotContains $player 'label: "SPEED"'           "Speed must be a gauge icon, not a SPEED text pill."
$audio = Get-Content (Join-Path $root "qml/AudioMenu.qml") -Raw
$subs  = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw
Assert-Contains    $audio 'PlayerIcon'                "AudioMenu face must render the languages icon."
Assert-NotContains $audio 'text: menu.chipValue'      "AudioMenu must not paint a text value on its face."
Assert-NotContains $audio 'menu.title.toUpperCase()'  "AudioMenu face must not be an AUDIO text pill."
Assert-Contains    $subs  'PlayerIcon'                "SubtitleMenu face must render the subtitles icon."
Assert-NotContains $subs  'text: menu.chipValue'      "SubtitleMenu must not paint a text value on its face."
Assert-Contains    $subs  'menu.active && !menu.panelOpen' "SubtitleMenu must show an active dot when subs are on."

# --- source metadata is PLAIN: no emoji pretending to be an icon ---
$srcSheet = Get-Content (Join-Path $root "qml/SourcesSheet.qml") -Raw
Assert-NotContains $srcSheet '\u{1F464}'    "Seeders must be plain text, no person emoji."
Assert-NotContains $srcSheet '\u{1F4BE}'    "Size must be plain text, no floppy emoji."
Assert-NotContains $srcSheet ([char]0x2699) "Source name must be plain text, no gear emoji."

Write-Host "Player semantic icon contract checks passed."
