# Contract: starting a download anywhere auto-reveals the OS-shell taskbar so the
# gold jobs badge is seen arriving (Hemanth 2026-07-18: "if I click download somewhere,
# it should automatically open the taskbar with the download tab having a number on it").
# Shape guarded here:
#   1. Main.qml watches LocalDownloads.changed and calls taskbar.reveal() when the
#      live-job count GROWS (never on finish/cancel — that's the `grew` gate).
#   2. While a reader/player owns the screen (immersiveSurfaceOpen) the reveal is
#      DEFERRED via pendingDownloadReveal and fired on return to the shell.
#   3. The badge itself stays wired: Taskbar.downloadsBadge fed from LocalDownloads totals.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw

# 1. the watcher: a Connections on LocalDownloads whose onChanged reveals the taskbar.
$watcher = [regex]::Match($main,
    'Connections\s*\{\s*target:\s*typeof LocalDownloads(?:(?!Connections\s*\{).)*?taskbar\.reveal\(\)',
    'Singleline').Value
if (-not $watcher) { throw 'no LocalDownloads watcher calling taskbar.reveal() — download start no longer pops the taskbar' }
if ($watcher -notmatch 'grew|>\s*win\.lastActiveDownloads') {
    throw 'download watcher reveals without a growth gate — the bar would pop on job FINISH too'
}

# 2. immersive deferral: the watcher must defer while a reader/player is up, and the
#    deferred reveal must fire from onImmersiveSurfaceOpenChanged.
if ($watcher -notmatch 'immersiveSurfaceOpen') {
    throw 'download watcher ignores immersiveSurfaceOpen — reveal() would fire invisibly under a reader/player'
}
if ($main -notmatch 'onImmersiveSurfaceOpenChanged[\s\S]*?pendingDownloadReveal[\s\S]*?taskbar\.reveal\(\)') {
    throw 'no deferred-reveal path on onImmersiveSurfaceOpenChanged — a download started in the player never surfaces'
}

# 3. the badge feed the reveal exists to show.
if ($main -notmatch 'downloadsBadge:\s*\(typeof LocalDownloads') {
    throw 'Taskbar.downloadsBadge is no longer fed from LocalDownloads — the revealed bar would show no number'
}

Write-Host 'test_taskbar_download_reveal_p0: PASS (download start reveals the bar, deferred while immersive, badge fed)'
