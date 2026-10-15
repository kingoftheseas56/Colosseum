$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\native\torrent\BookTorrentDownloader.cpp') -Raw
$header = Get-Content (Join-Path $PSScriptRoot '..\native\torrent\BookTorrentDownloader.h') -Raw

if ($source -notmatch 'QFutureWatcher<bool>') {
    throw 'Book torrent deletion must publish an asynchronous result.'
}
if ($source -notmatch 'QtConcurrent::run\(\[path\]') {
    throw 'Book torrent recursive deletion must run on the worker pool.'
}
if ($source -match 'QDir\(dirFor\(h\)\)\.removeRecursively\(\)') {
    throw 'Book torrent deletion must not recurse on the owner thread.'
}
if ($source -notmatch 'm_deleting\.contains\(infoHash\)') {
    throw 'Book torrent replacement must defer same-hash downloads during deletion.'
}
if ($source -notmatch 'm_deferredDownloads\.take\(h\)') {
    throw 'Deferred replacement requests must resume after deletion settles.'
}
if ($header -notmatch 'QSet<QString> m_deleting') {
    throw 'Book torrent deletion must track in-flight hash cleanup.'
}
Write-Output 'BOOK_TORRENT_DELETE_RESPONSIVENESS_CONTRACT_OK'
