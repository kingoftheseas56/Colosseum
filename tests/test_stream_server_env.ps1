$ErrorActionPreference = "Stop"

$streamPath = Join-Path $PSScriptRoot "..\native\player\streamserver.cpp"
$stream = Get-Content $streamPath -Raw

if ($stream -notmatch 'penv\.remove\(\s*QStringLiteral\("NODE_OPTIONS"\)\s*\)') {
    throw "StreamServer must remove inherited NODE_OPTIONS before launching stremio-runtime.exe."
}

if ($stream -notmatch 'NO_HTTPS_SERVER') {
    throw "StreamServer should preserve the no-HTTPS runtime environment."
}

Write-Host "StreamServer environment sanitization OK."
