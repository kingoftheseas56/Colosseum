$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$windowHeaderPath = Join-Path $root "native/player/windowmodestore.h"
$windowSourcePath = Join-Path $root "native/player/windowmodestore.cpp"

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

if (!(Test-Path $windowHeaderPath)) {
    throw "Native WindowModeStore header must exist for Harbor-style PiP/window mode parity."
}
if (!(Test-Path $windowSourcePath)) {
    throw "Native WindowModeStore implementation must exist for Harbor-style PiP/window mode parity."
}

$windowHeader = Get-Content $windowHeaderPath -Raw
$windowSource = Get-Content $windowSourcePath -Raw

# Harbor parity P0: PiP is native window mode, not a no-op button.
Assert-Contains $cmake "player/windowmodestore.cpp" `
    "CMake must compile WindowModeStore."
Assert-Contains $cmake "player/windowmodestore.h" `
    "CMake must include WindowModeStore."
Assert-Contains $main '#include "player/windowmodestore.h"' `
    "main.cpp must include WindowModeStore."
Assert-Contains $main 'setContextProperty(QStringLiteral("WindowMode")' `
    "main.cpp must expose WindowMode to QML."

Assert-Matches $windowHeader "class\s+WindowModeStore\s*:\s*public\s+QObject" `
    "WindowModeStore must be a QObject QML-facing state object."
Assert-Contains $windowHeader "Q_PROPERTY(bool pipMode" `
    "WindowModeStore must expose active PiP state."
Assert-Contains $windowHeader "Q_INVOKABLE void enterPip" `
    "WindowModeStore must expose native PiP enter."
Assert-Contains $windowHeader "Q_INVOKABLE void exitPip" `
    "WindowModeStore must expose native PiP exit."
Assert-Contains $windowHeader "QQuickWindow" `
    "WindowModeStore must operate on the actual QQuickWindow."
Assert-Contains $windowSource "WindowStaysOnTopHint" `
    "PiP enter must make the player window stay on top."
Assert-Contains $windowSource "FramelessWindowHint" `
    "PiP enter must preserve frameless theater chrome."
Assert-Contains $windowSource "showNormal" `
    "PiP enter/exit must leave fullscreen before resizing."
Assert-Contains $windowSource "setGeometry" `
    "PiP enter/exit must save and restore window geometry."
Assert-Contains $windowSource "availableGeometry" `
    "PiP enter must place the mini-player inside the current screen."
Assert-Contains $windowSource "480" `
    "PiP enter must use Harbor's 480px mini-player width."
Assert-Contains $windowSource "320" `
    "PiP enter must use Harbor's 320px mini-player height."
Assert-Contains $windowSource "pipEntered" `
    "WindowModeStore must signal PiP entered."
Assert-Contains $windowSource "pipExited" `
    "WindowModeStore must signal PiP exited."

Assert-Contains $player "property bool pipMode" `
    "PlayerPage must mirror PiP mode in QML state."
Assert-Contains $player "function togglePipMode" `
    "PlayerPage must expose a PiP toggle."
Assert-Contains $player "WindowMode.enterPip(root.Window.window)" `
    "PlayerPage must call native PiP enter with the real window."
Assert-Contains $player "WindowMode.exitPip(root.Window.window)" `
    "PlayerPage must call native PiP exit with the real window."
Assert-Contains $player "root.pipMode" `
    "PlayerPage controls must react to PiP state."
Assert-Contains $player "Qt.Key_P" `
    "PlayerPage must support the Harbor-style PiP keyboard shortcut."
Assert-Contains $player "icon: `"pip`"" `
    "PlayerPage transport controls must expose a PiP action."
Assert-Contains $player "tooltip: root.pipMode ? `"Exit PiP`" : `"Picture in picture`"" `
    "PlayerPage PiP control must show enter/exit intent."
Assert-Contains $player "visible: !root.pipMode" `
    "PlayerPage must suppress heavy chrome while in PiP."
Assert-Contains $player "onPipEntered" `
    "PlayerPage must listen for native PiP entered."
Assert-Contains $player "onPipExited" `
    "PlayerPage must listen for native PiP exited."

Write-Host "Player PiP P0 parity contract checks passed."
