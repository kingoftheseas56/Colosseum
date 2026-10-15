# Downloaded-video parity (spec docs/superpowers/specs/2026-07-06-colosseum-downloaded-video-parity.md).
# Downloads must ride the SAME check-in as streams: a Session (taskbar tile, honest minimize)
# with identity (Continue store, online subtitles), local resume from the Continue card, solid
# pop-up bodies, and the per-episode download back on the series page.
# NOTE: keep this file ASCII-only. PS 5.1 reads BOM-less files as ANSI; an em-dash decodes
# into a smart-quote byte (0x94) that PowerShell treats as a real quote and parsing explodes.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player  = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main    = Get-Content (Join-Path $root "qml/Main.qml") -Raw
$series  = Get-Content (Join-Path $root "qml/TheatreSeries.qml") -Raw
$audio   = Get-Content (Join-Path $root "qml/AudioMenu.qml") -Raw
$subs    = Get-Content (Join-Path $root "qml/SubtitleMenu.qml") -Raw
$style   = Get-Content (Join-Path $root "qml/SubStyleBar.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Count-Occurrences($text, $needle) {
    return ([regex]::Matches($text, [regex]::Escape($needle))).Count
}

# --- F1: session check-in with stream-grade identity ---
Assert-Contains $player 'function playLocalFile(target)' `
    "PlayerPage must expose playLocalFile - downloads get stream-grade identity."
Assert-Contains $player 'root.mediaLocalPath = String(t.localPath' `
    "playLocalFile must record the local path (resume identity)."
Assert-Contains $player 'root.fetchSubtitles()' `
    "playLocalFile / playTorrent must fetch online subtitles."
Assert-Contains $main 'function openLocalVideoSession(v)' `
    "Main must register downloaded videos as Sessions (taskbar tile, honest minimize)."
Assert-Contains $main 'playerLayer.item.playLocalFile(t)' `
    "activateSession must dispatch local targets to playLocalFile."
if ($main -like "*playerLayer.item.playUrl(item.path*") {
    throw "routeDownloadItem must NOT use the identity-less playUrl side door anymore."
}
Assert-Contains $player '"localPath": root.mediaLocalPath' `
    "recordProgress resume payload must carry localPath so Continue can reopen the file."

# --- session precision hooks (Main's pre-wired Task 5) ---
Assert-Contains $player 'function captureState()' `
    "PlayerPage must capture position on minimize (session precision)."
Assert-Contains $player 'function restoreState(st)' `
    "PlayerPage must restore position on taskbar re-open."
Assert-Contains $player 'root.pendingSeekSec = -1' `
    "playTorrent/playUrl must reset the pending seek (next-episode must not inherit it)."

# --- F2: pop-up bodies swallow clicks (the hollow-panel dismiss bug) ---
# Needle refreshed 2026-07-11 (stale-contract doctrine): the native player re-skin
# (forged-pane) collapsed the original 9-panel set to 8 — closeConfirm/overflow/
# liveGuide/dvr/abLoop/speed/fill + upNext — each verified to carry the swallower;
# dense PlayerMenu rows absorb their own clicks.
$swallower = 'MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.wakeChrome() }'
$playerSwallowers = Count-Occurrences $player $swallower
if ($playerSwallowers -lt 8) {
    throw "PlayerPage panels (closeConfirm/overflow/liveGuide/dvr/abLoop/speed/fill + upNext) must absorb body clicks (found $playerSwallowers of >=8)."
}
Assert-Contains $audio 'onClicked: {} }' `
    "AudioMenu panel body must absorb clicks (empty panels dismissed on any tap)."
Assert-Contains $subs 'onClicked: {} }' `
    "SubtitleMenu panel body must absorb clicks."
Assert-Contains $style 'onClicked: {} }' `
    "SubStyleBar body must absorb clicks."

# --- F3: Continue card resumes the local file, never a stream fetch ---
Assert-Contains $main 'if (r.localPath && String(r.localPath).length)' `
    "resumeContinue must branch to the local file BEFORE the infoHash stream path."

# --- F3b: Previous/Next and Up Next prefer a completed local episode ---
Assert-Contains $player 'function downloadedEpisodeTarget(ep)' `
    "PlayerPage must resolve a completed download for an adjacent episode."
Assert-Contains $player 'var localTarget = root.downloadedEpisodeTarget(ep)' `
    "jumpToEpisode must check the completed-download registry before stream resolution."
Assert-Contains $player 'root.playLocalFile(localTarget)' `
    "Adjacent navigation must open the completed local episode when available."
Assert-Contains $player 'var localCtx = t.playbackContext || ({})' `
    "Local episode playback must accept the target episode queue context."
Assert-Contains $player 'root.playbackQueue = localCtx.episodeQueue' `
    "Local episode playback must preserve the season queue."
Assert-Contains $player 'root.adjacentEpisodes = root.resolveAdjacentContext(localCtx)' `
    "Local episode playback must preserve Previous/Next after switching to disk."

# --- F4: per-episode download is back (signature gained the torrent-choice pick,
#         spec 2026-07-11 — the ↓ routes through the source picker now) ---
Assert-Contains $series 'function queueEpisodeDownload(v, pick)' `
    "TheatreSeries must queue a single episode again."
Assert-Contains $series 'id: epDl' `
    "Episode rows must carry the per-episode download button."
Assert-Contains $series 'queuedDownloadIds' `
    "The per-episode button must show queued state instead of re-queueing."

# --- headless load gate on the touched lazy surfaces ---
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) {
    throw "qml.exe not found at $qmlExe - update the Qt path in this test."
}
$harness = Join-Path $PSScriptRoot "parity_load_harness.qml"
$env:QT_FORCE_STDERR_LOGGING = "1"
$out = cmd /c "`"$qmlExe`" `"$harness`" 2>&1" | Out-String
if ($out -notlike "*ALL PARITY SURFACES READY*") {
    throw "A parity surface failed to instantiate. Loader output:`n$out"
}

Write-Host "Downloaded-video parity contract checks passed."
