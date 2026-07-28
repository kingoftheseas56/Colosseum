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

$snapshotMatch = [regex]::Match(
    $source,
    'QVariantMap MpvItem::dropProbeSnapshot\(\) const(?s).*?(?=void MpvItem::startDropProbe\(\))')
if (-not $snapshotMatch.Success) {
    throw 'MpvItem drop-probe snapshot implementation is missing.'
}
$snapshot = $snapshotMatch.Value
Require-Literal $snapshot 'getProperty(QStringLiteral("decoder-frame-drop-count"))' `
    'The probe decoder counter must use mpv decoder-frame-drop-count.'
Require-Literal $snapshot 'getProperty(QStringLiteral("frame-drop-count"))' `
    'The probe output counter must use mpv frame-drop-count.'
if ($snapshot.Contains('vo-drop-frame-count')) {
    throw 'The probe must not query removed mpv property vo-drop-frame-count.'
}
if ($snapshot -match 'getProperty\([^\r\n]+\)\.to(?:LongLong|Double|String|Bool)\(') {
    throw 'The probe snapshot must preserve invalid mpv values as JSON null.'
}
if ($source -notmatch 'm_dropProbeWarmupSeconds\s*<=\s*86400' -or
    $source -notmatch 'm_dropProbeMeasureSeconds\s*<=\s*86400') {
    throw 'The native probe must bound both timer values before converting seconds to milliseconds.'
}

Write-Output 'test_player_mpv_smoothness_p0: PASS'
