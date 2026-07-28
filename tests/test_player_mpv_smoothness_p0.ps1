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

$header = Get-Content -Raw (Join-Path $root 'native/player/mpvitem.h')
Require-Literal $source 'qEnvironmentVariable("COLOSSEUM_MPV_DROP_PROBE")' `
    'The drop probe must be explicitly environment-gated.'
Require-Literal $source 'MPV_DROP_PROBE RESULT ' `
    'The drop probe must publish one machine-readable final result.'
if ($header -notmatch 'void startDropProbe\(\);' -or
    $header -notmatch 'QVariantMap dropProbeSnapshot\(\) const;') {
    throw 'MpvItem must expose private start/snapshot probe seams.'
}

Write-Output 'test_player_mpv_smoothness_p0: PASS'
