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

# --- Task 2: the glass titlebar ---
Assert-Contains $player "id: titleBar" `
    "A fused glass titlebar must exist."
Assert-NotContains $player "GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.68) }" `
    "The Harbor top gradient scrim must be gone."
Assert-Contains $player 'id: titleBarVerbs' `
    "Window verbs (minimize/close) must live in the titlebar."

# The panel row must NOT carry window verbs anymore (they moved to the titlebar).
$controlsBlock = $player.Substring($player.IndexOf("id: transportRow"))
$controlsBlock = $controlsBlock.Substring(0, [Math]::Min(9000, $controlsBlock.Length))
Assert-NotContains $controlsBlock 'icon: "minimizeToBar"' `
    "The panel must not carry the minimize verb (titlebar owns it)."

# --- Task 3: live-value chips ---
Assert-Contains $player "component PanelChip" `
    "The chip component must exist."
Assert-Contains $player 'audioChipValue' `
    "Menus must surface their live value into the chip face."
Assert-Contains $player 'label: "EPISODES"' `
    "Episodes must be a labeled chip, not an anonymous icon."
$audio = Get-Content (Join-Path $root "qml/AudioMenu.qml") -Raw
Assert-Contains $audio "property string chipValue" `
    "AudioMenu's face must be a value chip."
$subs = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw
Assert-Contains $subs "property string chipValue" `
    "SubtitleMenu's face must be a value chip."

# --- Task 4: anchored applets ---
Assert-Contains $player "appletTail" `
    "Applet popovers must carry the pointer tail."
Assert-Contains $player "color: Qt.rgba(0.04, 0.05, 0.07, 0.94)" `
    "Popovers must wear the house popover surface."

# --- Task 5: the keepers wear the same cloth ---
$drawer = Get-Content (Join-Path $root "qml/BrowserDrawer.qml") -Raw
Assert-Contains $drawer "Qt.rgba(0.04, 0.05, 0.07, 0.94)" `
    "The episodes drawer must wear the house popover surface."

Write-Host "Native chrome contract checks passed."
