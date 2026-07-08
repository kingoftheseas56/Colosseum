$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

# --- Task 1: the fused Plasma panel (native chrome spec 2026-07-08) ---
Assert-Contains $player "color: Qt.rgba(0.04, 0.05, 0.07, 0.78)" `
    "The panel must wear the house glass film (taskbar family)."
Assert-Contains $player "id: panelHairline" `
    "The fused panel needs its top hairline."
Assert-NotContains $player "GradientStop { position: 0.38; color: Qt.rgba(0, 0, 0, 0.28) }" `
    "The Harbor bottom gradient scrim must be gone."
Assert-Contains $player "id: panelBreath" `
    "The panel must breathe (slide) with chrome visibility."

Write-Host "Native chrome contract checks passed."
