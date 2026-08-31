$ErrorActionPreference = "Stop"

$streamPath = Join-Path $PSScriptRoot "..\native\player\streamserver.cpp"
$headerPath = Join-Path $PSScriptRoot "..\native\player\streamserver.h"
$stream = Get-Content $streamPath -Raw
$header = Get-Content $headerPath -Raw

if ($stream -notmatch 'penv\.remove\(\s*QStringLiteral\("NODE_OPTIONS"\)\s*\)') {
    throw "StreamServer must remove inherited NODE_OPTIONS before launching stremio-runtime.exe."
}

if ($stream -notmatch 'NO_HTTPS_SERVER') {
    throw "StreamServer should preserve the no-HTTPS runtime environment."
}

if ($header -notmatch 'Q_PROPERTY\(bool engineUnavailable READ engineUnavailable NOTIFY engineUnavailableChanged\)') {
    throw "StreamServer must expose typed engine-unavailable state to the player."
}

if ($header -notmatch 'void engineUnavailableChanged\(\)') {
    throw "StreamServer must notify QML when engine availability changes."
}

if ($stream -notmatch 'setEngineUnavailable\(true\)') {
    throw "StreamServer must mark missing/failed runtime startup as unavailable."
}

Write-Host "StreamServer environment and failure-state contract OK."
