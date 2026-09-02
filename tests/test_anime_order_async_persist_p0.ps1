$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cpp = Get-Content (Join-Path $root 'native/anime/AnimeOrderService.cpp') -Raw

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$start = $cpp.IndexOf('void AnimeOrderService::onDownloadsReady')
$end = $cpp.IndexOf('void AnimeOrderService::launchParse', $start)
Need ($start -ge 0 -and $end -gt $start) 'AnimeOrderService download completion block must exist'
$block = $cpp.Substring($start, $end - $start)

Need ($block.Contains('QFutureWatcher')) `
    'Downloaded anime-order persistence must cross a worker watcher boundary.'
Need ($block.Contains('QtConcurrent::run')) `
    'Downloaded anime-order persistence/hash verification must run off the GUI thread.'
Need (-not $block.Contains('const QString genId = writeGeneration(fribb, xml, fetchedAt)')) `
    'QNetworkReply completion must not synchronously persist/hash the downloaded generation.'

Write-Host 'ANIME_ORDER_ASYNC_PERSIST_P0_OK'
