$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
function Read-File([string] $relative) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path)) { throw "MISSING FILE: $relative" }
    return Get-Content -LiteralPath $path -Raw
}
function Assert-Contains([string] $text, [string] $needle, [string] $message) {
    if (-not $text.Contains($needle)) { throw $message }
}
function Assert-Lacks([string] $text, [string] $needle, [string] $message) {
    if ($text.Contains($needle)) { throw $message }
}

$service = Read-File "native/engine/MangaTankobanService.cpp"
$header = Read-File "native/engine/MangaTankobanService.h"
$downloads = Read-File "native/engine/LocalDownloads.cpp"

# Production recovery is off the GUI thread and the UI-owned index never crosses affinity.
Assert-Contains $service 'm_recoveryReady = false;' `
    "Production service must expose a not-ready window while startup recovery runs."
Assert-Contains $service 'MangaVolumeIndex recovered(base);' `
    "Recovery must use a private worker-owned index."
Assert-Contains $service 'recovered.heal();' `
    "The worker must execute the existing self-heal implementation."
Assert-Contains $service 'QtConcurrent::run([base]()' `
    "Recovery must be scheduled on the worker pool."
Assert-Lacks $service 'm_index->heal();' `
    "The UI-owned index must not self-heal synchronously in the production constructor."

# Completion is the only readiness boundary; early transport/self-test work is retained.
Assert-Contains $header 'Q_PROPERTY(bool recoveryReady READ recoveryReady NOTIFY recoveryReadyChanged)' `
    "Readiness must be visible to QML and have a notify signal."
Assert-Contains $service 'emit recoveryReadyChanged();' `
    "Recovery completion must publish the readiness transition."
Assert-Contains $service 'm_pendingTransportFinishes' `
    "Transport completion must be buffered until the index is reloaded."
Assert-Contains $service 'm_pendingAcquired' `
    "Late acquisition completion must be buffered until readiness."
Assert-Contains $service 'm_pendingSelfTestSpec' `
    "The deterministic self-test must not race startup recovery."

$guards = ([regex]::Matches($service, 'if \(!m_recoveryReady\)')).Count
if ($guards -lt 6) {
    throw "Expected readiness guards on index-backed operations; found $guards."
}
Assert-Contains $downloads 'MangaTankobanService::recoveryReadyChanged' `
    "LocalDownloads must refresh after deferred volume recovery publishes its rows."

Write-Host "Tankoban startup recovery contract checks passed."
