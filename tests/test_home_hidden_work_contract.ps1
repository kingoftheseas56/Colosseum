$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

# Home's hero timer is a GUI animation. It must stop when the Home Flickable is hidden behind a
# world, while retaining its current index so returning Home does not rebuild or reset the shell.
Need ($main.Contains('interval: 6500; running: page.visible && !win.immersiveSurfaceOpen; repeat: true')) `
    'The Home hero timer must stand down while the Home page is hidden.'
Need ($main.Contains('visible: !win.immersiveSurfaceOpen')) `
    'The Home page visibility boundary must remain explicit.'
Need ($main.Contains('page.visible = false')) `
    'World navigation must continue to hide the Home page.'
Need ($main.Contains('page.visible = true')) `
    'Returning Home must continue to reveal the existing Home page state.'

Write-Host 'Home hidden-work contract: PASS'
