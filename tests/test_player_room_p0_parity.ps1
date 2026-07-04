$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw
$main = Get-Content (Join-Path $root "native/main.cpp") -Raw
$cmake = Get-Content (Join-Path $root "native/CMakeLists.txt") -Raw
$roomHeaderPath = Join-Path $root "native/player/roomstore.h"
$roomSourcePath = Join-Path $root "native/player/roomstore.cpp"

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

if (!(Test-Path $roomHeaderPath)) {
    throw "Native RoomStore header must exist for Harbor-style watch-room state."
}
if (!(Test-Path $roomSourcePath)) {
    throw "Native RoomStore implementation must exist for Harbor-style watch-room state."
}

$roomHeader = Get-Content $roomHeaderPath -Raw
$roomSource = Get-Content $roomSourcePath -Raw

# Harbor parity P0: the player needs a real room/together state model, not only buttons.
Assert-Contains $cmake "player/roomstore.cpp" `
    "CMake must compile the RoomStore implementation."
Assert-Contains $cmake "player/roomstore.h" `
    "CMake must include the RoomStore header."
Assert-Contains $main '#include "player/roomstore.h"' `
    "main.cpp must include RoomStore."
Assert-Contains $main 'setContextProperty(QStringLiteral("Room")' `
    "main.cpp must expose Room to QML as a context property."

Assert-Matches $roomHeader "class\s+RoomStore\s*:\s*public\s+QObject" `
    "RoomStore must be a QObject QML-facing state object."
Assert-Contains $roomHeader "Q_PROPERTY(bool active" `
    "RoomStore must expose active room state."
Assert-Contains $roomHeader "Q_PROPERTY(bool isHost" `
    "RoomStore must expose host/client role."
Assert-Contains $roomHeader "Q_PROPERTY(QVariantList participants" `
    "RoomStore must expose participants."
Assert-Contains $roomHeader "Q_PROPERTY(QVariantList chat" `
    "RoomStore must expose room chat."
Assert-Contains $roomHeader "Q_PROPERTY(QVariantMap syncState" `
    "RoomStore must expose synchronized playback state."
Assert-Contains $roomHeader "Q_INVOKABLE QString createLocalRoom" `
    "RoomStore must create a local room for the first transport slice."
Assert-Contains $roomHeader "Q_INVOKABLE void joinLocalRoom" `
    "RoomStore must support joining the local room model."
Assert-Contains $roomHeader "Q_INVOKABLE void markReady" `
    "RoomStore must track readiness."
Assert-Contains $roomHeader "Q_INVOKABLE void sendChat" `
    "RoomStore must support room chat."
Assert-Contains $roomHeader "Q_INVOKABLE void publishState" `
    "RoomStore must publish playback sync state."
Assert-Contains $roomHeader "Q_INVOKABLE void leaveRoom" `
    "RoomStore must support leaving a room."
Assert-Contains $roomHeader "syncCommand" `
    "RoomStore must emit sync commands for guest-side player application."
Assert-Contains $roomSource "participantId" `
    "RoomStore participants must carry stable ids."
Assert-Contains $roomSource "ready" `
    "RoomStore participants must carry readiness."
Assert-Contains $roomSource "position" `
    "RoomStore sync state must carry position."
Assert-Contains $roomSource "playing" `
    "RoomStore sync state must carry play/pause."

# Harbor parity P0: PlayerPage must have actual room controls and sync hooks.
Assert-Contains $player "property bool roomPanelOpen" `
    "PlayerPage must own a room/together panel state."
Assert-Contains $player "property bool applyingRoomSync" `
    "PlayerPage must guard against echoing guest sync application."
Assert-Contains $player "function createRoom" `
    "PlayerPage must create a watch room from the player."
Assert-Contains $player "function leaveRoom" `
    "PlayerPage must leave the current watch room."
Assert-Contains $player "function publishRoomState" `
    "PlayerPage must publish current playback state to Room."
Assert-Contains $player "function applyRoomSyncState" `
    "PlayerPage must apply host playback state as a guest."
Assert-Contains $player "function sendRoomChat" `
    "PlayerPage must send chat through Room."
Assert-Contains $player "Room.createLocalRoom" `
    "PlayerPage createRoom must call Room.createLocalRoom."
Assert-Contains $player "Room.publishState" `
    "PlayerPage must call Room.publishState."
Assert-Contains $player "Room.markReady" `
    "PlayerPage must call Room.markReady."
Assert-Contains $player "Room.sendChat" `
    "PlayerPage must call Room.sendChat."
Assert-Contains $player "Room.leaveRoom" `
    "PlayerPage must call Room.leaveRoom."
Assert-Contains $player "Connections {" `
    "PlayerPage must connect to Room signals."
Assert-Contains $player "target: Room" `
    "PlayerPage must listen to Room."
Assert-Contains $player "onSyncCommand" `
    "PlayerPage must react to room sync commands."
Assert-Contains $player "roomPublishTimer" `
    "PlayerPage must periodically publish host playback state."
Assert-Contains $player "Watch room" `
    "PlayerPage chrome must expose a watch-room affordance."
Assert-Contains $player "Room chat" `
    "PlayerPage must expose room chat UI."
Assert-Contains $player "Start synced playback" `
    "PlayerPage must expose a synchronized start control."
Assert-Contains $player "Ready" `
    "PlayerPage must expose readiness controls."
Assert-Contains $player "Leave room" `
    "PlayerPage must expose a leave-room control."

Write-Host "Player room P0 parity contract checks passed."
