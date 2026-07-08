$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: player needs an on-video drawing mode with temporary strokes.
Assert-Contains $player "property bool drawMode" `
    "PlayerPage must track draw mode."
Assert-Contains $player "property var drawStrokes" `
    "PlayerPage must store draw strokes."
Assert-Contains $player "property string drawColor" `
    "PlayerPage must expose the local drawing color."
Assert-Contains $player "function startDrawStroke" `
    "PlayerPage must start normalized draw strokes."
Assert-Contains $player "function addDrawPoint" `
    "PlayerPage must append normalized draw points."
Assert-Contains $player "function endDrawStroke" `
    "PlayerPage must finish draw strokes."
Assert-Contains $player "function pruneDrawStrokes" `
    "PlayerPage must expire old draw strokes like Harbor."
Assert-Contains $player "drawGcTimer" `
    "PlayerPage must run draw stroke garbage collection."
Assert-Contains $player "id: drawCanvas" `
    "PlayerPage must render draw strokes on a canvas."
Assert-Contains $player "drawInputArea" `
    "PlayerPage must capture pointer input while drawing."
Assert-Contains $player "onPressed: root.startDrawStroke" `
    "Draw input must start a stroke on press."
Assert-Contains $player "onPositionChanged: root.addDrawPoint" `
    "Draw input must add points while dragging."
Assert-Contains $player "onReleased: root.endDrawStroke" `
    "Draw input must end strokes on release."
Assert-Contains $player '"label": "Draw", "kind": "draw"' `
    "Draw must live as an overflow-menu row (ToolsMenu retired 2026-07-08)."
Assert-Contains $player 'else if (kind === "draw") root.toggleDrawMode()' `
    "The Draw row must actually toggle draw mode."

Write-Host "Player draw mode P0 parity contract checks passed."
