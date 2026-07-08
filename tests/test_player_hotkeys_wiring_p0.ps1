$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") { throw $message }
}

Assert-Contains $player 'import "PlayerHotkeys.js" as PlayerHotkeys' `
    "PlayerPage must import the pure hotkey registry."
Assert-Contains $player "ShortcutsSheet" `
    "PlayerPage must render the shortcuts sheet."
Assert-Contains $player "property bool shortcutsOpen" `
    "PlayerPage must track whether the shortcuts sheet is open."
Assert-Contains $player "function handlePlayerHotkey" `
    "PlayerPage must route key presses through a named handler."
Assert-Contains $player "function runHotkeyAction" `
    "PlayerPage must dispatch registry action ids through one switch."
Assert-Contains $player "PlayerHotkeys.actionForEvent" `
    "PlayerPage must ask PlayerHotkeys to resolve key events."

Assert-NotContains $player "Qt.Key_F" `
    "F must not toggle fullscreen/windowed mode in the fullscreen-only player."
Assert-NotContains $player "toggleWindowFullscreen" `
    "The player must not expose a window fullscreen toggle."
Assert-NotContains $player "onDoubleClicked: if (!root.anyMenuOpen) root.toggleWindowFullscreen()" `
    "Double-click must not leave fullscreen-only mode."

Write-Host "Player hotkey wiring checks passed."
