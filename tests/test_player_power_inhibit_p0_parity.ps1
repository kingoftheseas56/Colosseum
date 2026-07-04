$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$powerHeaderPath = Join-Path $root "native/player/powerstore.h"
$powerSourcePath = Join-Path $root "native/player/powerstore.cpp"

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) {
        throw $message
    }
}

if (!(Test-Path $powerHeaderPath)) {
    throw "Native PowerStore header must exist for Harbor-style playback power inhibit."
}
if (!(Test-Path $powerSourcePath)) {
    throw "Native PowerStore implementation must exist for Harbor-style playback power inhibit."
}

$powerHeader = Get-Content $powerHeaderPath -Raw
$powerSource = Get-Content $powerSourcePath -Raw

# Harbor parity P0: active playback must prevent OS/display sleep and release on pause/exit.
Assert-Contains $cmake "player/powerstore.cpp" `
    "CMake must compile PowerStore."
Assert-Contains $cmake "player/powerstore.h" `
    "CMake must include PowerStore."
Assert-Contains $main '#include "player/powerstore.h"' `
    "main.cpp must include PowerStore."
Assert-Contains $main 'setContextProperty(QStringLiteral("Power")' `
    "main.cpp must expose Power to QML."

Assert-Matches $powerHeader "class\s+PowerStore\s*:\s*public\s+QObject" `
    "PowerStore must be a QObject QML-facing state object."
Assert-Contains $powerHeader "Q_PROPERTY(bool inhibited" `
    "PowerStore must expose inhibited state."
Assert-Contains $powerHeader "Q_INVOKABLE void setInhibited" `
    "PowerStore must expose a QML command to toggle inhibition."
Assert-Contains $powerHeader "Q_INVOKABLE void release" `
    "PowerStore must expose explicit release for player teardown."
Assert-Contains $powerHeader "inhibitedChanged" `
    "PowerStore must notify QML when inhibit state changes."
Assert-Contains $powerSource "SetThreadExecutionState" `
    "Windows builds must use SetThreadExecutionState for playback inhibit."
Assert-Contains $powerSource "ES_SYSTEM_REQUIRED" `
    "Power inhibit must keep the system awake while playing."
Assert-Contains $powerSource "ES_DISPLAY_REQUIRED" `
    "Power inhibit must keep the display awake while playing."
Assert-Contains $powerSource "ES_CONTINUOUS" `
    "Power inhibit must release with ES_CONTINUOUS."

Assert-Contains $player "function syncPowerInhibit" `
    "PlayerPage must centralize power inhibit sync from playback state."
Assert-Contains $player "Power.setInhibited" `
    "PlayerPage must call native power inhibit."
Assert-Contains $player "Power.release" `
    "PlayerPage must release power inhibit on teardown/exit."
Assert-Contains $player "onPauseChanged: {" `
    "PlayerPage must update power inhibit when play/pause changes."
Assert-Contains $player "onFileLoaded:" `
    "PlayerPage must update power inhibit when mpv starts active playback."
Assert-Contains $player "onEndFile:" `
    "PlayerPage must update power inhibit when mpv playback ends."
Assert-Contains $player "Component.onDestruction: if (typeof Power !== `"undefined`") Power.release()" `
    "PlayerPage must release inhibit when the player is destroyed."

Write-Host "Player power inhibit P0 parity contract checks passed."
