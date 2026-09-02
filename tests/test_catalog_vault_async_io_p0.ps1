$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cpp = Get-Content -Raw (Join-Path $root 'native\engine\CatalogVaultClient.cpp')
$hdr = Get-Content -Raw (Join-Path $root 'native\engine\CatalogVaultClient.h')

function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Require (($cpp + $hdr) -match 'CatalogVaultIoWorker') `
    'CatalogVault must own a dedicated I/O worker.'
Require ($cpp -match 'moveToThread') `
    'CatalogVault I/O worker must execute on a non-GUI QThread.'
Require ($cpp -notmatch 'm_downloadFile->write') `
    'Network readyRead must not write catalog bytes through the GUI-owned QFile.'
Require ($hdr -notmatch 'QFile\* m_downloadFile') `
    'CatalogVaultClient must not own the download QFile on the GUI thread.'

Write-Output 'CATALOG_VAULT_ASYNC_IO_P0_OK'
