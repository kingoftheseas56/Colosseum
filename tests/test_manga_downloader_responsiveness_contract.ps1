$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\native\engine\MangaDownloader.cpp') -Raw
$header = Get-Content (Join-Path $PSScriptRoot '..\native\engine\MangaDownloader.h') -Raw

if ($source -notmatch 'startResumeScan\(job\)') {
    throw 'MangaDownloader must defer the resume filesystem scan.'
}
if ($source -notmatch 'QtConcurrent::run\(\[dirPath, total\]') {
    throw 'Resume scanning must run on the worker pool.'
}
if ($source -notmatch 'lifetime \? lifetime->job : nullptr') {
    throw 'Resume completion must resolve the job through its lifetime guard.'
}
if ($source -notmatch 'if \(!job \|\| job->cancelled\)') {
    throw 'Resume completion must drop cancelled/destroyed jobs.'
}
if ($source -notmatch 'saveImageAsync\(job, pageIndex, attempt, name, data\)') {
    throw 'Accepted image bytes must use the asynchronous publication seam.'
}
if ($source -notmatch 'QtConcurrent::run\(\[outputPath, data\]') {
    throw 'Image publication must run QSaveFile on the worker pool.'
}
if ($source -notmatch 'onImageSaved\(job, pageIndex, fileName, result\.size\)') {
    throw 'Image publication must apply Job mutation on the owner thread.'
}
if ($source -notmatch 'startIndexWrite\(\)') {
    throw 'Index persistence must use the serialized async publication seam.'
}
if ($source -notmatch 'QtConcurrent::run\(\[outputPath, payload\]') {
    throw 'Index persistence must run QSaveFile on the worker pool.'
}
if ($source -notmatch 'QSaveFile file\(outputPath\)' -or $source -notmatch 'file\.commit\(\)') {
    throw 'Index persistence must retain atomic QSaveFile commit semantics.'
}
if ($source -notmatch 'm_indexWriteInFlight = false') {
    throw 'Index persistence must release its single-writer gate on completion.'
}
if ($source -notmatch 'm_pendingIndexSnapshot') {
    throw 'Index persistence must retain the newest snapshot while a write is active.'
}
if ($source -notmatch 'if \(!m_pendingIndexSnapshot\.isNull\(\)\)\s+startIndexWrite\(\)') {
    throw 'Index persistence must serialize the newest snapshot after the active write.'
}
if ($header -notmatch 'struct IndexWriteResult') {
    throw 'Index persistence must use a bounded value result.'
}
if ($source -notmatch 'runWhenIndexIdle\(\[this, realDir\]') {
    throw 'The index self-test must await async persistence without blocking the owner thread.'
}
if ($header -notmatch 'struct ResumeScan') {
    throw 'Resume scan result must be a bounded value object.'
}
Write-Output 'MANGA_DOWNLOADER_RESPONSIVENESS_CONTRACT_OK'
