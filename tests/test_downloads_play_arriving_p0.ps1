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

function Assert-Contains($text, $needle, $message) { if ($text -notlike "*$needle*") { throw $message } }

# DownloadStore::jobs() exposes the resolved url ("" until resolved — honest).
Assert-Contains $store '{QStringLiteral("url"), j.url}' "DownloadStore::jobs() must expose the job's resolved url."

# LocalDownloads theatre map passes url + art through to the page.
Assert-Contains $local '{QStringLiteral("url"), j.value(QStringLiteral("url"))}' "LocalDownloads theatre jobs must pass the url through."
Assert-Contains $local '{QStringLiteral("art"), j.value(QStringLiteral("art"))}' "LocalDownloads theatre jobs must pass the art through."

Write-Host "PASS: read-model exposes url + art for arriving theatre jobs."
