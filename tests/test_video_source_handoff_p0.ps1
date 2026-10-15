$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw
$theatre = Get-Content (Join-Path $root "qml/TheatreSeries.qml") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$player2Host = Get-Content (Join-Path $root "qml/player2host/ColosseumHostServices.qml") -Raw
$player2Page = Get-Content (Join-Path $root "qml/player2host/Player2Page.qml") -Raw
$storeH = Get-Content (Join-Path $root "native/player/downloadstore.h") -Raw
$storeC = Get-Content (Join-Path $root "native/player/downloadstore.cpp") -Raw

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

# Continuity guard: the existing arriving URL + part-file handoff is valid and must stay valid.
Assert-Matches $main 'function\s+routeArrivingPlay\(job\)[\s\S]*?"streamUrl"\s*:\s*job\.url[\s\S]*?"partPath"\s*:\s*part' `
    "routeArrivingPlay must carry streamUrl plus partPath into the session."
Assert-Matches $main 'function\s+activateSession\(rec\)[\s\S]*?t\.arrivingUrl\s*=\s*t\.streamUrl[\s\S]*?playLocalFile\(t\)' `
    "activateSession must synthesize arrivingUrl when starting from a growing part file."

# F0008-1: resolver must admit Direct rows without sending url: pseudo-hashes to Stream.
Assert-Matches $storeH 'Q_INVOKABLE\s+void\s+feedSource\(' `
    "DownloadStore needs a source feed that carries URL plus request headers."
Assert-Matches $main 'function\s+resolveDownloadJob[\s\S]*?streamKind\s*===\s*"Direct"[\s\S]*?Download\.feedSource' `
    "Direct resolver rows must feed DownloadStore directly instead of torrent prefetch."
# F0008-2: request-header provenance must survive download + arriving playback.
Assert-Matches $theatre 'function\s+applyPick[\s\S]*?req\["headers"\]\s*=[\s\S]*?pick\.headers' `
    "A picked Direct source must retain its request headers in the durable job request."
Assert-Matches $storeC 'startHttp\(Job\s*&job\)[\s\S]*?request\.value\(QStringLiteral\("headers"\)\)[\s\S]*?setRawHeader' `
    "DownloadStore HTTP acquisition must install source request headers."
Assert-Matches $storeC 'QVariantList\s+DownloadStore::jobs\(\)\s+const[\s\S]*?QStringLiteral\("headers"\)' `
    "DownloadStore jobs must project request headers to arriving playback."
Assert-Matches $main 'function\s+routeArrivingPlay\(job\)[\s\S]*?"headers"\s*:\s*job\.headers' `
    "routeArrivingPlay must carry request headers into the session target."
Assert-Matches $player 'property\s+var\s+arrivingStreamHeaders' `
    "Disk-first arriving playback must retain headers for frontier fallback."
Assert-Matches $player 'function\s+switchArrivingToStream\(\)[\s\S]*?var\s+headers\s*=\s*root\.arrivingStreamHeaders[\s\S]*?"headers"\s*:\s*headers' `
    "Frontier fallback must preserve source request headers."
Assert-Matches $player 'function\s+playRemoteUrl\(target\)[\s\S]*?loadDirectStreamUrl\([^,]+,\s*t\.headers\)' `
    "Arriving remote playback must use the header-aware direct URL loader."
Assert-Matches $player 'function\s+startVideoDownload\(\)[\s\S]*?sourceHeaders[\s\S]*?"headers"\s*:\s*sourceHeaders' `
    "Player 1 manual download must preserve the current Direct source headers."
Assert-Matches $player2Host 'function\s+requestDownload\([^)]*\)[\s\S]*?_currentCandidate\(\)[\s\S]*?"headers"\s*:\s*sourceHeaders' `
    "Player 2 manual download must preserve the selected/current source headers."
Assert-Matches $player2Page 'function\s+_open\(url,\s*headers\)[\s\S]*?"headers"\s*:\s*requestHeaders' `
    "Player 2 direct playback must hand request headers into Player2Backend."

# F0008-3: Player 2 episode/source resolution must keep the requested identity and transport rows.
Assert-Matches $player2Host 'function\s+requestAlternateSources\(mediaId\)[\s\S]*?requestedId[\s\S]*?var\s+id\s*=\s*requestedId' `
    "Player 2 alternate-source lookup must resolve the requested media id."
Assert-Matches $player2Host 'function\s+finish\(fetched\)[\s\S]*?host\.streamCandidates\s*=\s*fetched' `
    "Fetched Player 2 sources must remain transport-grade candidates, not display-only rows."
Assert-Matches $player2Page 'onAlternateSourcesResolved[\s\S]*?page\.subStreamId\s*=\s*episodeId[\s\S]*?_switchToSource' `
    "Player 2 episode switches must bind the new episode identity before source handoff."

Write-Host "PASS: Function 0008 source-resolution and handoff contract holds."