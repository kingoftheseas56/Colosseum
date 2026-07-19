# The per-show cinematic loader component (PlayerLoadingScreen.qml): full-bleed blurred art,
# centered logo-or-title, uppercase episode line, a status line, and a THIN INDETERMINATE bar.
# It must never invent a torrent readiness percentage / peer count / transfer speed.
# docs/superpowers/specs/2026-07-19-colosseum-harbor-player-polish-design.md

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$file = Join-Path $root "qml/PlayerLoadingScreen.qml"

function Assert-File($path, $message) { if (-not (Test-Path $path)) { throw $message } }
function Assert-Contains($text, $needle, $message) { if ($text -notlike "*$needle*") { throw $message } }
function Assert-NotContains($text, $needle, $message) { if ($text -like "*$needle*") { throw $message } }

Assert-File $file "qml/PlayerLoadingScreen.qml must exist."
$ls = Get-Content $file -Raw

# --- public API PlayerPage binds to ---
foreach ($prop in @('property string title','property string episodeLine','property url    logoUrl',
                    'property url    backdropUrl','property string statusText','property bool   active',
                    'property bool   errored')) {
    Assert-Contains $ls $prop "PlayerLoadingScreen must expose: $prop"
}
Assert-Contains $ls 'signal cancelRequested()' "PlayerLoadingScreen must expose the cancelRequested() signal."

# --- logo preference, title fallback ---
Assert-Contains $ls '!logo.visible && root.title.length > 0' "Title must show only when the logo is absent (logo preferred)."
Assert-Contains $ls 'toUpperCase()' "The episode line must be uppercased."

# --- art decodes ONLY while active; Stremio shows the backdrop CLEAR (not blurred) ---
Assert-Contains    $ls 'source: root.active ? root.backdropUrl : ""' "Backdrop must decode only while active."
Assert-NotContains $ls 'blurEnabled'                                 "The Stremio backdrop must be shown clear, not blurred."
Assert-Contains    $ls '300' "The loader must use a 300 ms opacity transition."

# --- INDETERMINATE bar: a sweeping segment, and ABSOLUTELY no fabricated readiness numbers ---
Assert-Contains    $ls 'loops: Animation.Infinite' "The bar must sweep continuously (indeterminate)."
Assert-NotContains $ls 'peers'   "The loader must not show a peer count (no trustworthy number exists)."
Assert-NotContains $ls 'downloadSpeed' "The loader must not show a transfer speed."
Assert-NotContains $ls 'percent' "The loader must not show a percentage."
Assert-NotContains $ls 'progressValue' "The loader must not bind a determinate progress value."

# --- cancel is a single signal, error text/retry owned by PlayerPage ---
Assert-Contains $ls 'onClicked: root.cancelRequested()' "The cancel affordance must emit cancelRequested."

Write-Host "Player loading-screen component contract checks passed."
