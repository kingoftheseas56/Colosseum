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

Write-Host 'test_fullscreen_controls_p0: shell transition contract PASS'
