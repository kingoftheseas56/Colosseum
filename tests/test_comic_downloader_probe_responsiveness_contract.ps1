$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\native\engine\ComicDownloader.cpp') -Raw
$header = Get-Content (Join-Path $PSScriptRoot '..\native\engine\ComicDownloader.h') -Raw

if ($source -notmatch 'void ComicDownloader::ingestArchiveByProbe\(InFlight& f\)') {
    throw 'ComicDownloader archive probe entry point is missing.'
}
if ($source -notmatch 'runPackOrCopyThenPublish\(serial,') {
    throw 'Archive probing must use the existing owner-thread completion seam.'
}
if ($source -notmatch 'QtConcurrent::run\(std::move\(work\)\)') {
    throw 'Archive probing must execute through the worker pool.'
}
if ($source -notmatch 'CbzArchive::probe\(archivePath, &error\)') {
    throw 'The worker must probe the captured archive path by value.'
}
if ($source -match 'const MangaTankoban::CbzProbeResult probe = MangaTankoban::CbzArchive::probe\(f\.archivePath\)') {
    throw 'The owner thread must not probe f.archivePath inline.'
}
if ($source -notmatch 'f\.packing = true') {
    throw 'Probe cancellation must use the existing in-flight worker protection.'
}
if ($source -notmatch 'result\.cleanupPathsOnDiscard = \{archivePath\}') {
    throw 'A retired probe must retain ownership of source cleanup.'
}
if ($source -notmatch 'inFlightPaths') {
    throw 'Retired-worker cleanup must protect replacement job paths.'
}
if ($source -notmatch 'active\.packing = false') {
    throw 'Probe completion must release the worker ownership latch before routing.'
}
if ($header -notmatch 'including the initial\s+// archive probe') {
    throw 'The lifetime contract must document probe worker ownership.'
}

Write-Output 'COMIC_DOWNLOADER_PROBE_RESPONSIVENESS_CONTRACT_OK'
