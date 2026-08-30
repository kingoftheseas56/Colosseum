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
if ($header -notmatch 'struct ResumeScan') {
    throw 'Resume scan result must be a bounded value object.'
}
Write-Output 'MANGA_DOWNLOADER_RESPONSIVENESS_CONTRACT_OK'
