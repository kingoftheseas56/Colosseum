import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// Playback probe for the PRODUCTION Player 2 page (Task 17). The page probe proves it constructs;
// this proves it actually PAINTS. It opens a real local file through the same playLocalFile() entry
// point Main.qml uses, lets it run, then reports the engine's own frame counters.
//
// It exists because "audio plays, picture is black" is invisible to every other check we have: the
// contracts pass, the page constructs, the session reaches Playing - and nothing renders. Only the
// presented-frame count tells the truth.
//
// Run (from the repo root, with COLOSSEUM_PLAYER2=1 so the process boots on D3D11):
//   colosseum.exe tests/player2/player2_play_probe.qml
// Pass the real local clip via COLOSSEUM_ABBA_CLIP; the probe fails closed if it is absent.
Window {
    id: probe
    width: 960
    height: 540
    visible: true          // MUST be visible: a hidden window never renders, so nothing ever presents
    title: "Player 2 playback probe"
    color: "black"

    property string clip: ""
    property int ticks: 0
    // A local file has no network source, so networkStalled must stay false for the whole play. If
    // it ever went true here the chrome would sit on a permanent, lying "Buffering".
    property bool everStalled: false

    Player2Page {
        id: page
        anchors.fill: parent
    }

    Timer {
        id: settle
        interval: 1000
        repeat: true
        running: true
        onTriggered: {
            probe.ticks += 1
            var d = page.diagnosticsSnapshot()
            if (page.sessionNetworkStalled())
                probe.everStalled = true
            console.log("PROBE tick=" + probe.ticks + " state=" + page.sessionState()
                        + " networkStalled=" + page.sessionNetworkStalled()
                        + " status=[" + page.statusText() + "]")
            if (probe.ticks === 3) {
                // One full dump early on, so a zero counter can be traced to its cause instead of
                // guessed at (state? never opened? decoded but never presented?).
                console.log("PROBE state=" + (page.sessionState()) + " dur=" + page.sessionDuration()
                            + " keys=" + JSON.stringify(d))
            }
            if (probe.ticks >= 18 || (d && Number(d.presented) > 30)) {
                settle.running = false
                var presented = d ? Number(d.presented || 0) : 0
                var decoded = d ? Number(d.decoded || 0) : 0
                var errors = d ? Number(d.deviceErrors || 0) : 0
                if (presented > 0 && decoded > 0 && errors === 0 && !probe.everStalled)
                    console.log("PLAYER2 PLAY PROBE: PASS decoded=" + decoded
                                + " presented=" + presented + " deviceErrors=" + errors
                                + " everStalled=" + probe.everStalled)
                else
                    console.log("PLAYER2 PLAY PROBE: FAIL decoded=" + decoded
                                + " presented=" + presented + " deviceErrors=" + errors
                                + " everStalled=" + probe.everStalled
                                + "  (presented=0 means the picture is black;"
                                + " everStalled=true means a local file reported a network stall)")
                Qt.callLater(function() { Qt.quit() })
            }
        }
    }

    Component.onCompleted: {
        probe.clip = (typeof DevAbbaClip !== "undefined") ? String(DevAbbaClip) : ""
        if (probe.clip.length === 0) {
            console.log("PLAYER2 PLAY PROBE: FAIL set COLOSSEUM_ABBA_CLIP to a local media file")
            Qt.callLater(function() { Qt.quit() })
            return
        }
        page.playLocalFile({ "id": "probe:local", "title": "probe clip", "localPath": probe.clip })
    }
}
