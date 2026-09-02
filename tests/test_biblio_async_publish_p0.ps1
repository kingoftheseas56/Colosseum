$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cppPath = Join-Path $root 'native\engine\BiblioCatalog.cpp'
$hPath = Join-Path $root 'native\engine\BiblioCatalog.h'
$cpp = Get-Content -LiteralPath $cppPath -Raw
$h = Get-Content -LiteralPath $hPath -Raw

function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Require ($cpp -notmatch 'm_store\.publish\s*\(\s*snapshot\s*\)') `
    'Biblio snapshot publish must not run through the GUI-owned store.'
Require ($cpp -match 'QtConcurrent::run[\s\S]*BiblioCatalogStore\s+publisher') `
    'Biblio snapshot publish must use a worker-owned BiblioCatalogStore.'
Require ($cpp -match 'publisher\.open\s*\(\s*dbPath\s*\)') `
    'Worker publisher must open its own SQLite connection from the database path.'
Require ($cpp -match 'publisher\.publish\s*\(\s*snapshot\s*\)') `
    'Worker publisher must perform the snapshot transaction off the GUI thread.'
Require ($h -match 'QString\s+m_dbPath\s*;') `
    'BiblioCatalog must retain the SQLite path for worker-owned publisher connections.'

Write-Host 'BIBLIO_ASYNC_PUBLISH_P0_OK'
