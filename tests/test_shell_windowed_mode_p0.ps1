$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$behavior = Get-Content (Join-Path $root 'qml/WindowBehavior.qml') -Raw
$store = Get-Content (Join-Path $root 'native/player/windowmodestore.h') -Raw

function Require-Text($text, $needle, $message) {
    if (-not $text.Contains($needle)) { throw $message }
}
function Reject-Text($text, $needle, $message) {
    if ($text.Contains($needle)) { throw $message }
}

Require-Text $main 'sequences: ["F11"]' 'Main must expose the secret F11 door.'
Require-Text $main 'context: Qt.ApplicationShortcut' 'F11 must work across every route and player.'
Require-Text $main 'WindowMode.toggleShellMode(win)' 'F11 must delegate to the native authority.'
Require-Text $main 'WindowMode.initializeShell(win)' 'Native state must choose startup presentation.'
Require-Text $main 'WindowBehavior {' 'Main must install chrome-free move/resize behavior.'
Reject-Text $main 'if (win.visibility === Window.Windowed) win.visibility = Window.FullScreen' 'Restore must preserve the current base mode.'
Reject-Text $main 'toggleWindowFullscreen' 'The retired player/window toggle must not return.'

Require-Text $behavior 'required property Window shell' 'Behavior must target the one root window.'
Require-Text $behavior 'required property Item dragSurface' 'Behavior must reuse the existing TopBar.'
Require-Text $behavior 'startSystemMove' 'Unused TopBar space must use native system move.'
Require-Text $behavior 'toggleMaximized' 'TopBar double-click must maximize/restore.'
Require-Text $behavior 'Window.Maximized' 'TopBar double-click must remain available while maximized.'
Require-Text $behavior 'readonly property bool resizable' 'Resize zones must be distinct from maximized interaction.'
Require-Text $behavior 'startSystemResize' 'Edges must use native system resize.'
Require-Text $behavior 'Qt.TopEdge | Qt.LeftEdge' 'Top-left corner resize must exist.'
Require-Text $behavior 'Qt.BottomEdge | Qt.RightEdge' 'Bottom-right corner resize must exist.'
Require-Text $store 'Q_PROPERTY(bool shellWindowed' 'Native base mode must be visible to QML.'

Write-Host 'test_shell_windowed_mode_p0: PASS'
