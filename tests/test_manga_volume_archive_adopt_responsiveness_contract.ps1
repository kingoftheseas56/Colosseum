$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\native\engine\MangaVolumeArchiveIngestor.cpp') -Raw
$indexSource = Get-Content (Join-Path $PSScriptRoot '..\native\engine\MangaVolumeIndex.cpp') -Raw
$indexHeader = Get-Content (Join-Path $PSScriptRoot '..\native\engine\MangaVolumeIndex.h') -Raw

if ($source -notmatch 'QFutureWatcher<CbzAdoptResult>') {
    throw 'CBZ adoption must publish a value-only asynchronous result.'
}
if ($source -notmatch 'QtConcurrent::run\(\[sourcePath, finalPath, partPath\]') {
    throw 'CBZ source validation/copy must run on the worker pool.'
}
if ($source -notmatch 'CbzArchive::imageEntries\(sourcePath, &result\.error\)') {
    throw 'The worker must validate the captured source path.'
}
if ($source -notmatch 'QFile::copy\(sourcePath, partPath\)') {
    throw 'The worker must own the potentially large source-to-part copy.'
}
if ($source -notmatch 'CbzArchive::imageEntries\(partPath, &result\.error\)') {
    throw 'The worker must validate the staged copy before publication.'
}
if ($source -match 'CbzArchive::imageEntries\(m_active->archivePath') {
    throw 'Owner-thread source archive inspection must be removed.'
}
if ($source -match 'QFile::copy\(m_active->archivePath') {
    throw 'Owner-thread source archive copy must be removed.'
}
if ($source -notmatch 'QFile::rename\(partPath, finalPath\)') {
    throw 'Atomic final rename must remain on the owner thread.'
}
if ($source -notmatch 'm_index->publishArchiveValidated\(m_active->record') {
    throw 'Validated archive adoption must use the owner-thread publication seam.'
}
if ($indexSource -notmatch 'publishArchiveValidated\(' -or
    $indexSource -notmatch 'archiveAlreadyValidated') {
    throw 'The index must expose a value-validated archive publication path.'
}
if ($indexHeader -notmatch 'publishArchiveValidated\(') {
    throw 'The validated archive publication seam must be declared.'
}
if ($source -notmatch 'QTimer::singleShot\(kSourceOpenRetryMs') {
    throw 'Flush-race retry behavior must remain intact after worker validation.'
}

Write-Output 'MANGA_VOLUME_ARCHIVE_ADOPT_RESPONSIVENESS_CONTRACT_OK'
