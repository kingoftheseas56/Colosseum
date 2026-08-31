$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content (Join-Path $root "native\player\streamserver.h") -Raw
$stream = Get-Content (Join-Path $root "native\player\streamserver.cpp") -Raw
$player = Get-Content (Join-Path $root "qml\PlayerPage.qml") -Raw

function Require([bool]$condition, [string]$message) {
    if (-not $condition) {
        throw "STREAM_ENGINE_FAILFAST_FAIL: $message"
    }
}

Require ($header -match 'Q_PROPERTY\(bool engineUnavailable READ engineUnavailable NOTIFY engineUnavailableChanged\)') `
    "StreamServer must expose typed engine-unavailable state to the player."
Require ($header -match 'bool engineUnavailable\(\) const') `
    "StreamServer must expose an engineUnavailable accessor."
Require ($header -match 'void engineUnavailableChanged\(\)') `
    "StreamServer must notify QML when engine availability changes."
Require ($stream -match 'setEngineUnavailable\(true\)') `
    "StreamServer must mark missing or failed runtime startup as unavailable."
Require ($stream -match 'Streaming engine unavailable\. Repair or reinstall Colosseum\.') `
    "StreamServer must provide an actionable deployment error."
Require ($player -match 'function\s+onStreamError\(message\)[\s\S]*?Stream\.engineUnavailable[\s\S]*?streamWatchdog\.stop\(\)[\s\S]*?root\.errored\s*=\s*true[\s\S]*?root\.starting\s*=\s*false[\s\S]*?return') `
    "PlayerPage must fail honestly when the streaming engine is unavailable instead of retrying every source."

Write-Host "Stream engine fail-fast contract passed."
