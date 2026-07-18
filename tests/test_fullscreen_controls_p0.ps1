$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
$shield = Join-Path $root 'qml/FullscreenTransitionShield.qml'

if (-not (Test-Path $shield)) {
    throw 'FullscreenTransitionShield.qml is missing'
}

$shieldText = Get-Content $shield -Raw
if ($main -notmatch 'FullscreenTransitionShield\s*\{') {
    throw 'Main does not own the fullscreen shield'
}
if ($main -notmatch 'function toggleFullscreenShell\(\)[\s\S]{0,250}fullscreenTransition\.begin\(\)') {
    throw 'all fullscreen requests must enter through the shield'
}
if ($main -notmatch 'onActivated:\s*win\.toggleFullscreenShell\(\)') {
    throw 'F11 bypasses the shared transition path'
}
if ($shieldText -notmatch 'interval:\s*250') {
    throw 'shield lost its bounded frame fallback'
}
if ($shieldText -notmatch 'function onFrameSwapped\(\)') {
    throw 'shield does not wait for a presented frame'
}

$player = Get-Content (Join-Path $root 'qml/PlayerPage.qml') -Raw
if ($player -notmatch 'signal fullscreenRequested\(\)') {
    throw 'PlayerPage lost fullscreenRequested()'
}
if ($player -notmatch 'kind === "fullscreenExit"') {
    throw 'PlayerPage has no hand-drawn restore glyph'
}
if ($player -notmatch 'icon:\s*root\.shellWindowed\s*\?\s*"fullscreen"\s*:\s*"fullscreenExit"') {
    throw 'player titlebar does not show the available fullscreen action'
}
if ($player -notmatch 'onClicked:\s*root\.fullscreenRequested\(\)') {
    throw 'player fullscreen button does not emit the semantic request'
}
if ($main -notmatch 'item\.fullscreenRequested\.connect\(win\.toggleFullscreenShell\)') {
    throw 'playerLayer does not route fullscreen through Main'
}

$bookFullscreen = Join-Path $root 'assets/icons/reader2/fullscreen.svg'
$bookFullscreenExit = Join-Path $root 'assets/icons/reader2/fullscreen-exit.svg'
if (-not (Test-Path $bookFullscreen) -or -not (Test-Path $bookFullscreenExit)) {
    throw 'book reader fullscreen SVG pair is incomplete'
}
$bookTop = Get-Content (Join-Path $root 'qml/reader2/TopBar.qml') -Raw
$bookChrome = Get-Content (Join-Path $root 'qml/reader2/ReaderChrome.qml') -Raw
$bookShell = Get-Content (Join-Path $root 'qml/reader2/ReaderShell.qml') -Raw
foreach ($entry in @(
    @{ Text = $bookTop; Name = 'TopBar' },
    @{ Text = $bookChrome; Name = 'ReaderChrome' },
    @{ Text = $bookShell; Name = 'ReaderShell' }
)) {
    if ($entry.Text -notmatch 'signal fullscreenRequested\(\)') {
        throw "$($entry.Name) lost fullscreenRequested()"
    }
}
if ($bookTop -notmatch 'root\.shellWindowed[\s\S]{0,180}reader2/fullscreen\.svg[\s\S]{0,180}reader2/fullscreen-exit\.svg') {
    throw 'book TopBar does not swap the reader-specific fullscreen action icon'
}
if ($bookChrome -notmatch 'onFullscreenRequested:\s*chrome\.fullscreenRequested\(\)') {
    throw 'ReaderChrome does not forward the TopBar fullscreen request'
}
if ($bookShell -notmatch 'onFullscreenRequested:\s*shell\.fullscreenRequested\(\)') {
    throw 'ReaderShell does not forward the chrome fullscreen request'
}
if ($main -notmatch 'item\.minimized\.connect\(win\.minimizeBookReader\)[\s\S]{0,160}item\.fullscreenRequested\.connect\(win\.toggleFullscreenShell\)') {
    throw 'bookReaderLayer does not route fullscreen through Main'
}

Write-Host 'test_fullscreen_controls_p0: shell + player + book contract PASS'
