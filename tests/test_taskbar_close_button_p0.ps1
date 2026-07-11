# Taskbar video-tab close button P0 contract (spec 2026-07-11). SHAPE ONLY.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function MustContain($file, $needle, $why) {
    $p = Join-Path $root $file
    if (!(Select-String -Path $p -Pattern ([regex]::Escape($needle)) -Quiet)) {
        throw "MISSING in ${file}: '$needle' ($why)"
    }
}

# single-session tile close: its own MouseArea id, closes sessions[0], reveal handler present
MustContain "qml/Taskbar.qml" 'id: tileClose'                          "single-tile close affordance exists"
MustContain "qml/Taskbar.qml" 'id: tileCloseMa'                        "tile close has its own hit area (topmost in its corner)"
MustContain "qml/Taskbar.qml" 'bar.closeRequested(tile.modelData.sessions[0].id)' "tile X closes the one session"
MustContain "qml/Taskbar.qml" 'HoverHandler { id: tileHover }'         "reveal driven by a composing HoverHandler, not the click MouseArea"

# fan close fix: the close Item outranks the full-row switch handler
MustContain "qml/Taskbar.qml" '// close outranks rowMa'                "fan close Item lifted above rowMa (z)"

# regression guard: Main still routes close -> closeSession
MustContain "qml/Main.qml" 'onCloseRequested: (id) => win.closeSession(id)' "close plumbing intact"

Write-Host "Taskbar close button P0 contract passed."
