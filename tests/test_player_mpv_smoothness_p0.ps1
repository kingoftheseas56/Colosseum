$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw (Join-Path $root 'native/player/mpvitem.cpp')

function Require-Literal([string]$haystack, [string]$needle, [string]$message) {
    if (-not $haystack.Contains($needle)) { throw $message }
}

Require-Literal $source 'setProperty(QStringLiteral("video-sync"), QStringLiteral("display-resample"));' `
    'MpvItem must synchronize video to the display clock.'
Require-Literal $source 'setProperty(QStringLiteral("interpolation"), QStringLiteral("yes"));' `
    'MpvItem must interpolate non-integer source/display cadence.'

Write-Output 'test_player_mpv_smoothness_p0: PASS'
