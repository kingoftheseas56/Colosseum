param([string]$RootOverride = "")
$ErrorActionPreference = 'Stop'
$root = if ($RootOverride) { $RootOverride } else { Split-Path -Parent $PSScriptRoot }
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw

# Download reveal is driven by one aggregate count, not a LocalDownloads-only watcher.
# This keeps LocalDownloads and Audiobooks on the same badge/reveal contract.
if ($main -notmatch 'readonly property int totalActiveDownloads:[\s\S]{0,320}Audiobooks\.activeCount') {
    throw 'totalActiveDownloads must aggregate LocalDownloads and Audiobooks'
}
if ($main -notmatch 'downloadsBadge:\s*win\.totalActiveDownloads') {
    throw 'Taskbar badge must use the same aggregate count as reveal arbitration'
}

# Only growth reveals. Finish/cancel must not pop the taskbar.
$growth = [regex]::Match($main,
    'onTotalActiveDownloadsChanged:\s*\{[\s\S]{0,420}?taskbar\.reveal\(\)',
    'Singleline').Value
if (-not $growth) { throw 'missing total-download growth reveal handler' }
if ($growth -notmatch 'totalActiveDownloads\s*>\s*lastActiveDownloads') {
    throw 'download reveal handler lost its growth gate'
}
if ($growth -notmatch 'lastActiveDownloads\s*=\s*totalActiveDownloads') {
    throw 'download reveal handler must advance its comparison baseline'
}
if ($growth -notmatch 'if\s*\(!grew\)\s*return') {
    throw 'download finish/cancel can reveal the taskbar because !grew is not gated'
}

# Immersive surfaces defer the pop, then reveal it once the reader/player leaves.
if ($growth -notmatch 'immersiveSurfaceOpen[\s\S]{0,80}pendingDownloadReveal\s*=\s*true') {
    throw 'download reveal no longer defers while an immersive surface owns the screen'
}
$deferred = [regex]::Match($main,
    'onImmersiveSurfaceOpenChanged:\s*\{[\s\S]{0,280}?taskbar\.reveal\(\)',
    'Singleline').Value
if (-not $deferred) { throw 'missing deferred taskbar reveal after immersive exit' }
if ($deferred -notmatch '!immersiveSurfaceOpen\s*&&\s*pendingDownloadReveal') {
    throw 'deferred reveal no longer requires leaving the immersive surface'
}
if ($deferred -notmatch 'pendingDownloadReveal\s*=\s*false') {
    throw 'deferred reveal flag is not consumed before reveal'
}
if ($deferred -notmatch 'lastActiveDownloads\s*>\s*0') {
    throw 'deferred reveal can pop after all downloads already finished'
}

Write-Host 'test_taskbar_download_reveal_p0: PASS (aggregate growth reveals; immersive starts defer; badge shares the count)'
