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

Write-Host 'test_fullscreen_controls_p0: shell + player contract PASS'
