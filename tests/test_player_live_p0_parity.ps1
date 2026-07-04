$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$liveHeaderPath = Join-Path $root "native/player/livestore.h"
$liveSourcePath = Join-Path $root "native/player/livestore.cpp"

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

if (!(Test-Path $liveHeaderPath)) {
    throw "Native LiveStore header must exist for Harbor-style live/DVR player state."
}
if (!(Test-Path $liveSourcePath)) {
    throw "Native LiveStore implementation must exist for Harbor-style live/DVR player state."
}

$liveHeader = Get-Content $liveHeaderPath -Raw
$liveSource = Get-Content $liveSourcePath -Raw

# Harbor parity P0: live playback needs real channel and DVR state, not static labels.
Assert-Contains $cmake "player/livestore.cpp" `
    "CMake must compile LiveStore."
Assert-Contains $cmake "player/livestore.h" `
    "CMake must include LiveStore."
Assert-Contains $main '#include "player/livestore.h"' `
    "main.cpp must include LiveStore."
Assert-Contains $main 'setContextProperty(QStringLiteral("Live")' `
    "main.cpp must expose Live to QML as a context property."

Assert-Matches $liveHeader "class\s+LiveStore\s*:\s*public\s+QObject" `
    "LiveStore must be a QObject QML-facing state object."
Assert-Contains $liveHeader "Q_PROPERTY(bool isLive" `
    "LiveStore must expose whether current playback is live."
Assert-Contains $liveHeader "Q_PROPERTY(QVariantMap activeChannel" `
    "LiveStore must expose the active channel."
Assert-Contains $liveHeader "Q_PROPERTY(QVariantList channels" `
    "LiveStore must expose switchable channels."
Assert-Contains $liveHeader "Q_PROPERTY(QString group" `
    "LiveStore must expose the selected live group."
Assert-Contains $liveHeader "Q_PROPERTY(QString query" `
    "LiveStore must expose the guide search query."
Assert-Contains $liveHeader "Q_PROPERTY(QVariantList recordings" `
    "LiveStore must expose DVR recording sessions."
Assert-Contains $liveHeader "Q_INVOKABLE void setLiveChannel" `
    "LiveStore must accept the current channel from PlayerPage."
Assert-Contains $liveHeader "Q_INVOKABLE void addChannel" `
    "LiveStore must support adding/switching channel rows."
Assert-Contains $liveHeader "Q_INVOKABLE void setGroup" `
    "LiveStore must support group filtering."
Assert-Contains $liveHeader "Q_INVOKABLE void setQuery" `
    "LiveStore must support guide search."
Assert-Contains $liveHeader "Q_INVOKABLE void switchChannel" `
    "LiveStore must emit channel switch requests."
Assert-Contains $liveHeader "Q_INVOKABLE QString startRecording" `
    "LiveStore must start DVR sessions."
Assert-Contains $liveHeader "Q_INVOKABLE void stopRecording" `
    "LiveStore must stop DVR sessions."
Assert-Contains $liveHeader "channelSwitchRequested" `
    "LiveStore must signal channel switches to QML."
Assert-Contains $liveSource "recording" `
    "LiveStore DVR sessions must carry recording state."
Assert-Contains $liveSource "elapsedSec" `
    "LiveStore DVR sessions must carry elapsed time."
Assert-Contains $liveSource "plannedDurationSec" `
    "LiveStore DVR sessions must carry planned duration."

# Harbor parity P0: PlayerPage must expose live guide and DVR controls over the player.
Assert-Contains $player "property bool liveGuideOpen" `
    "PlayerPage must own live guide overlay state."
Assert-Contains $player "property bool dvrPanelOpen" `
    "PlayerPage must own DVR panel state."
Assert-Contains $player "function configureLiveChannel" `
    "PlayerPage must configure current live channel state."
Assert-Contains $player "function openLiveGuide" `
    "PlayerPage must open the live channel guide."
Assert-Contains $player "function switchLiveChannel" `
    "PlayerPage must switch live channels."
Assert-Contains $player "function startDvrRecording" `
    "PlayerPage must start DVR recording."
Assert-Contains $player "function stopDvrRecording" `
    "PlayerPage must stop DVR recording."
Assert-Contains $player "function goLiveEdge" `
    "PlayerPage must expose jump-to-live behavior."
Assert-Contains $player "Live.setLiveChannel" `
    "PlayerPage must call Live.setLiveChannel."
Assert-Contains $player "Live.switchChannel" `
    "PlayerPage must call Live.switchChannel."
Assert-Contains $player "Live.startRecording" `
    "PlayerPage must call Live.startRecording."
Assert-Contains $player "Live.stopRecording" `
    "PlayerPage must call Live.stopRecording."
Assert-Contains $player "target: Live" `
    "PlayerPage must listen to Live signals."
Assert-Contains $player "onChannelSwitchRequested" `
    "PlayerPage must handle Live channel switch requests."
Assert-Contains $player "Live channel" `
    "PlayerPage must expose live channel text."
Assert-Contains $player "Live guide" `
    "PlayerPage must expose a live guide overlay."
Assert-Contains $player "Search channels" `
    "PlayerPage live guide must support channel search."
Assert-Contains $player "DVR record" `
    "PlayerPage must expose DVR record controls."
Assert-Contains $player "Start recording" `
    "PlayerPage DVR panel must start recording."
Assert-Contains $player "Stop recording" `
    "PlayerPage DVR panel must stop recording."
Assert-Contains $player "Jump to live edge" `
    "PlayerPage must expose live-edge control."
Assert-Contains $player "icon: `"live`"" `
    "PlayerPage chrome must expose live guide action."
Assert-Contains $player "icon: `"record`"" `
    "PlayerPage chrome must expose DVR action."

Write-Host "Player live P0 parity contract checks passed."
