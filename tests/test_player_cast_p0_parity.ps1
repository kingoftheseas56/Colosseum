$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$castHeaderPath = Join-Path $root "native/player/caststore.h"
$castSourcePath = Join-Path $root "native/player/caststore.cpp"

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

if (!(Test-Path $castHeaderPath)) {
    throw "Native CastStore header must exist for Harbor-style cast session state."
}
if (!(Test-Path $castSourcePath)) {
    throw "Native CastStore implementation must exist for Harbor-style cast session state."
}

$castHeader = Get-Content $castHeaderPath -Raw
$castSource = Get-Content $castSourcePath -Raw

# Harbor parity P0: casting needs a real device/session state model.
Assert-Contains $cmake "player/caststore.cpp" `
    "CMake must compile CastStore."
Assert-Contains $cmake "player/caststore.h" `
    "CMake must include CastStore."
Assert-Contains $main '#include "player/caststore.h"' `
    "main.cpp must include CastStore."
Assert-Contains $main 'setContextProperty(QStringLiteral("Cast")' `
    "main.cpp must expose Cast to QML as a context property."

Assert-Matches $castHeader "class\s+CastStore\s*:\s*public\s+QObject" `
    "CastStore must be a QObject QML-facing state object."
Assert-Contains $castHeader "Q_PROPERTY(bool scanning" `
    "CastStore must expose scanning state."
Assert-Contains $castHeader "Q_PROPERTY(bool active" `
    "CastStore must expose active casting state."
Assert-Contains $castHeader "Q_PROPERTY(bool pending" `
    "CastStore must expose pending connection state."
Assert-Contains $castHeader "Q_PROPERTY(bool playing" `
    "CastStore must expose cast play/pause state."
Assert-Contains $castHeader "Q_PROPERTY(double position" `
    "CastStore must expose cast position."
Assert-Contains $castHeader "Q_PROPERTY(QVariantList devices" `
    "CastStore must expose discovered devices."
Assert-Contains $castHeader "Q_PROPERTY(QVariantMap device" `
    "CastStore must expose the connected device."
Assert-Contains $castHeader "Q_PROPERTY(QString error" `
    "CastStore must expose cast errors."
Assert-Contains $castHeader "Q_PROPERTY(QString localServerUrl" `
    "CastStore must expose local server/address status."
Assert-Contains $castHeader "Q_INVOKABLE void discoverDevices" `
    "CastStore must support device discovery/rescan."
Assert-Contains $castHeader "Q_INVOKABLE void startCasting" `
    "CastStore must start a cast session."
Assert-Contains $castHeader "Q_INVOKABLE void stopCasting" `
    "CastStore must stop a cast session."
Assert-Contains $castHeader "Q_INVOKABLE void play" `
    "CastStore must support cast play."
Assert-Contains $castHeader "Q_INVOKABLE void pause" `
    "CastStore must support cast pause."
Assert-Contains $castHeader "Q_INVOKABLE void seek" `
    "CastStore must support cast seek."
Assert-Contains $castSource "chromecast" `
    "CastStore device rows must carry kind labels."
Assert-Contains $castSource "manual" `
    "CastStore must provide a manual/local receiver until SSDP transport lands."
Assert-Contains $castSource "position" `
    "CastStore session state must carry position."

# Harbor parity P0: PlayerPage must expose cast menu/session controls over the player.
Assert-Contains $player "property bool castPanelOpen" `
    "PlayerPage must own a cast panel state."
Assert-Contains $player "function openCastPanel" `
    "PlayerPage must open/rescan cast devices."
Assert-Contains $player "function startCast" `
    "PlayerPage must start casting the current media."
Assert-Contains $player "function stopCast" `
    "PlayerPage must stop casting."
Assert-Contains $player "function toggleCastPlay" `
    "PlayerPage must toggle cast playback."
Assert-Contains $player "function seekCast" `
    "PlayerPage must seek the cast session."
Assert-Contains $player "Cast.discoverDevices" `
    "PlayerPage must call Cast.discoverDevices."
Assert-Contains $player "Cast.startCasting" `
    "PlayerPage must call Cast.startCasting."
Assert-Contains $player "Cast.stopCasting" `
    "PlayerPage must call Cast.stopCasting."
Assert-Contains $player "Cast.play" `
    "PlayerPage must call Cast.play."
Assert-Contains $player "Cast.pause" `
    "PlayerPage must call Cast.pause."
Assert-Contains $player "Cast.seek" `
    "PlayerPage must call Cast.seek."
Assert-Contains $player "Cast to TV or speaker" `
    "PlayerPage must expose Harbor's cast menu language."
Assert-Contains $player "No Chromecast, DLNA, or Roku devices found" `
    "PlayerPage must explain the empty cast-device state."
Assert-Contains $player "Casting to" `
    "PlayerPage must expose a cast session bar."
Assert-Contains $player "Stop casting" `
    "PlayerPage must expose stop casting."
Assert-Contains $player "icon: `"cast`"" `
    "PlayerPage chrome must expose a cast action."

Write-Host "Player cast P0 parity contract checks passed."
