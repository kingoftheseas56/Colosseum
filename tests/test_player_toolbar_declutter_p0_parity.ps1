$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor-style declutter: occasional tools live behind ONE tools (...) menu button
# instead of a flat wall of ~15 icons that overflow and overlap the transport.

# --- The tools menu component + its glyph ---
Assert-Contains $player "component ToolsMenu" `
    "PlayerPage must define a ToolsMenu popover for occasional tools."
Assert-Contains $player 'kind === "more"' `
    "IconGlyph must draw a 'more' (...) glyph for the tools button."
Assert-Contains $player 'icon: "more"' `
    "The tools menu button must use the 'more' glyph."
Assert-Contains $player "property var actions" `
    "ToolsMenu must take a list of tool actions."

# --- The menu instance wired into the control bar ---
Assert-Contains $player "id: toolsMenu" `
    "The control bar must host a tools menu instance."

# --- Participates in menu bookkeeping (close others / chrome-stays-open) ---
Assert-Contains $player "toolsMenu.panelOpen = false" `
    "closeMenus must also close the tools menu."
Assert-Contains $player "|| toolsMenu.panelOpen" `
    "anyMenuOpen must count the tools menu so chrome stays up while it's open."

# --- Occasional tools relocated into the menu (labels only the menu has) ---
Assert-Contains $player '"Record GIF"' `
    "Record GIF must move into the tools menu."
Assert-Contains $player '"Playback stats"' `
    "Playback stats must move into the tools menu."
Assert-Contains $player '"Screenshot"' `
    "Frame grab (Screenshot) must move into the tools menu."

Write-Host "Player toolbar declutter P0 parity contract checks passed."
