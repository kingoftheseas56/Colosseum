# Play-while-arriving: a LIVE theatre download job must surface its resolved url
# (and art) through the read-model chain DownloadStore::jobs() ->
# LocalDownloads::activeJobs() so the Downloads page can offer Play on it.
# The engine/resolver lane (needResolve/feedUrl) is untouched — this only
# exposes the already-resolved url. QML wiring asserts land in Tasks 3-4.
# Plan: docs/superpowers/plans/2026-07-20-colosseum-play-while-arriving.md

$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $PSScriptRoot
$store = Get-Content (Join-Path $root "native/player/downloadstore.cpp") -Raw
$local = Get-Content (Join-Path $root "native/engine/LocalDownloads.cpp") -Raw

# Literal substring check (NOT -like: needles carry [0] indexing, which -like
# would treat as a wildcard character class and never match).
function Assert-Contains($text, $needle, $message) { if (-not $text.Contains($needle)) { throw $message } }

# DownloadStore::jobs() exposes the resolved url ("" until resolved — honest).
Assert-Contains $store '{QStringLiteral("url"), j.url}' "DownloadStore::jobs() must expose the job's resolved url."

# LocalDownloads theatre map passes url + art through to the page.
Assert-Contains $local '{QStringLiteral("url"), j.value(QStringLiteral("url"))}' "LocalDownloads theatre jobs must pass the url through."
Assert-Contains $local '{QStringLiteral("art"), j.value(QStringLiteral("art"))}' "LocalDownloads theatre jobs must pass the art through."

$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

# The player grows a direct-url lane: transport "Arriving", no localPath, same
# stream-grade identity (subtitles by id, progress by id) as playLocalFile.
Assert-Contains $player 'function playRemoteUrl(target)' "PlayerPage must have the playRemoteUrl lane."
Assert-Contains $player 'root.mediaTransport = "Arriving"' "Arriving sessions must be labeled Arriving (subtitle line tail)."
Assert-Contains $player 'mpv.loadFile(root.currentPlaybackUrl)' "playRemoteUrl must load the url it was handed."

# Direct-url sessions have no candidate ladder: one reconnect, then an honest fail
# (retryCurrentStream() early-returns with no candidates -> would loop as a no-op).
Assert-Contains $player 'The stream dropped. The download keeps going' "Failure ladder needs the honest direct-url branch."

$page = Get-Content (Join-Path $root "qml/DownloadsPage.qml") -Raw
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw

# Downloads page: live theatre rows offer Play only while downloading with a url.
Assert-Contains $page 'signal playArrivingRequested(var job)' "DownloadsPage must emit playArrivingRequested."
Assert-Contains $page 'root.playArrivingRequested(grp.modelData.rows[0])' "Single arriving card must wire Play."
Assert-Contains $page 'root.playArrivingRequested(epRow.modelData)' "Per-episode arriving row must wire Play."

# Host: routes to a session whose target carries streamUrl; resume position read
# from the same Progress key the landed copy uses.
Assert-Contains $main 'function routeArrivingPlay(job)' "Main must route arriving play."
Assert-Contains $main '"streamUrl": job.url' "Arriving session target must carry the job url."
Assert-Contains $main 'playArrivingRequested.connect(win.routeArrivingPlay)' "DownloadsPage signal must be connected."
# Dispatcher: streamUrl branch between localPath and torrent; restart-restore prefers
# a since-landed local copy over a possibly-dead url.
Assert-Contains $main 'playerLayer.item.playRemoteUrl(t)' "Session dispatcher needs the streamUrl branch."
Assert-Contains $main 'function downloadedVideoPath(id)' "Landed-restore guard needs the path lookup."

Write-Host "PASS: read-model exposes url + art for arriving theatre jobs."
