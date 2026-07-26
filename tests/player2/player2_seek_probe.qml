import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// Seek probe for the PRODUCTION page (Task 17). Hemanth reported the player CLOSING on any seek;
// the run log showed the session entering Error after the first frame, which our handler answers by
// closing. The lab's seek soak passes 100/100, so this reproduces the same action through the
// production page to find what differs. Deterministic: play, wait for real frames, then seek.
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    title: "Player 2 seek probe"

    property int ticks: 0
    property bool seeked: false
    property string phase: "warmup"

    Player2Page { id: page; anchors.fill: parent }

    Timer {
        interval: 500; repeat: true; running: true
        onTriggered: {
            probe.ticks += 1
            var d = page.diagnosticsSnapshot()
            var presented = Number((d && d.presented) || 0)
            var state = page.sessionState()

            if (!probe.seeked && presented > 25) {
                probe.seeked = true
                probe.phase = "seeking"
                console.log("SEEK PROBE: pre-seek OK presented=" + presented + " state=" + state
                            + " pos=" + page.sessionPosition())
                page.sessionSeek(120.0)
                return
            }
            if (probe.seeked && probe.ticks % 4 === 0) {
                console.log("SEEK PROBE: post-seek state=" + state + " presented=" + presented
                            + " pos=" + page.sessionPosition()
                            + " deviceLost=" + ((d && d.deviceLostReason) || "?")
                            + " recovery=" + ((d && d.recoveryAttempts) || 0))
            }
            if (probe.ticks > 40) {
                console.log("SEEK PROBE: DONE finalState=" + state + " presented=" + presented)
                Qt.callLater(function() { Qt.quit() })
            }
        }
    }

    Connections {
        target: page
        function onBackendRestartRequired(reason) {
            console.log("SEEK PROBE: FAIL session errored after first frame -> " + reason)
        }
        function onBackendFallback(reason) {
            console.log("SEEK PROBE: FALLBACK -> " + reason)
        }
    }

    Component.onCompleted: page.playLocalFile({
        "id": "probe:seek", "title": "seek probe",
        "localPath": "C:/Users/Suprabha/Downloads/Colosseum/The Wire - S4E10 - Misgivings - 20260720_175049.mp4"
    })
}
