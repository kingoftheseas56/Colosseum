import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// The two things a viewer DOES during a stalled seek, proved end to end. player2_stream_seek_probe
// proves the wait is visible and lands; this proves the player still obeys and still behaves while
// the wait is happening.
//
// 1. PAUSE LANDS. During the stall the transport renders a live Pause button (buffering is true).
//    Pressing it must not be dropped. Player2Session::pause() sees Seeking and steers
//    m_postSeekState, and seekCompleted reconciles the worker - which had already latched the
//    play/pause state it was told at seek time. So the seek must complete in Paused(4) AT THE
//    TARGET: not Playing (the press ignored), not still Seeking (the seek broken).
//
// 2. THE CHROME RE-ARMS. hideTimer holds while buffering/paused. It has no `repeat`, so before the
//    re-arm it fired once, declined to hide, and DIED - leaving the chrome up forever after a
//    stalled seek until the next mouse move. This probe never moves the mouse, so controlsShown
//    going false on its own after playback resumes is the whole proof.
//
// Drive it with the window fixture, which must let the bytes arrive so the seek can land:
//   powershell -NoProfile -File tests/player2/player2_http_fixture_server.ps1 `
//     -File artifacts/streamclip.mp4 -Port 8791 -Mode window -WindowBytes 4194304 `
//     -WindowOpenAfterSec 40
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    title: "Player 2 stall-intent probe"

    property int ticks: 0
    property bool seeked: false
    property bool pausePressed: false
    property bool pauseLanded: false
    property bool resumed: false
    property int resumedAtTick: 0
    property bool chromeReArmed: false
    property real seekTarget: 60.0

    Player2Page { id: page; anchors.fill: parent }

    Timer {
        interval: 500; repeat: true; running: true
        onTriggered: {
            probe.ticks += 1
            var d = page.diagnosticsSnapshot()
            var presented = Number((d && d.presented) || 0)
            var state = page.sessionState()
            var stalled = page.sessionNetworkStalled()
            var pos = page.sessionPosition()
            var chrome = page.chromeShown()

            if (!probe.seeked) {
                if (presented > 25) {
                    probe.seeked = true
                    console.log("STALL INTENT PROBE: pre-seek OK presented=" + presented
                                + " state=" + state + " pos=" + pos.toFixed(2))
                    console.log("STALL INTENT PROBE: seeking to " + probe.seekTarget
                                + "s (past the served window)")
                    page.sessionSeek(probe.seekTarget)
                }
                return
            }

            console.log("STALL INTENT PROBE: tick=" + probe.ticks + " state=" + state
                        + " networkStalled=" + stalled + " chromeShown=" + chrome
                        + " pausePressed=" + probe.pausePressed + " pos=" + pos.toFixed(2))

            // --- 1. press pause partway INTO the stall -------------------------------------------
            if (!probe.pausePressed && stalled && state === 5) {
                probe.pausePressed = true
                console.log("STALL INTENT PROBE: pressing PAUSE during the stall (state=5)")
                page.sessionPause()
                // The press must NOT take effect yet - the seek is still in flight.
                if (page.sessionState() !== 5)
                    console.log("STALL INTENT PROBE: FAIL pause left Seeking immediately -> state="
                                + page.sessionState())
                return
            }

            // --- the seek lands: it must land PAUSED, at the target ------------------------------
            if (probe.pausePressed && !probe.pauseLanded && state !== 5) {
                probe.pauseLanded = true
                var atTarget = pos >= (probe.seekTarget - 2.0)
                console.log("STALL INTENT PROBE: seek landed state=" + state + " pos=" + pos.toFixed(2)
                            + " (expect state=4 Paused, pos>=" + (probe.seekTarget - 2.0) + ")")
                if (state !== 4 || !atTarget) {
                    console.log("STALL INTENT PROBE: FAIL-PAUSE state=" + state + " pos=" + pos.toFixed(2))
                    Qt.callLater(function() { Qt.quit() })
                    return
                }
                console.log("STALL INTENT PROBE: PAUSE-LANDED ok")
                return
            }

            // --- 2. resume, then watch the chrome hide itself with NO mouse input ----------------
            if (probe.pauseLanded && !probe.resumed) {
                probe.resumed = true
                probe.resumedAtTick = probe.ticks
                console.log("STALL INTENT PROBE: resuming; chrome must auto-hide with no mouse input")
                page.sessionPlay()
                return
            }
            if (probe.resumed && !probe.chromeReArmed && !chrome) {
                probe.chromeReArmed = true
                console.log("STALL INTENT PROBE: CHROME-REARMED after "
                            + ((probe.ticks - probe.resumedAtTick) * 0.5).toFixed(1)
                            + "s of playback, no mouse input")
            }

            // hideTimer's buffering/paused interval is 4500ms; give it a generous margin.
            if (probe.chromeReArmed || (probe.resumed && probe.ticks - probe.resumedAtTick > 40)
                || probe.ticks > 700) {
                console.log("STALL INTENT PROBE: " + (probe.chromeReArmed ? "PASS" : "FAIL-CHROME")
                            + " pauseLanded=" + probe.pauseLanded
                            + " chromeReArmed=" + probe.chromeReArmed
                            + " finalState=" + page.sessionState() + " pos=" + pos.toFixed(2))
                Qt.callLater(function() { Qt.quit() })
            }
        }
    }

    Component.onCompleted: page.playRemoteUrl({
        "id": "probe:stall-intent", "title": "stall intent probe",
        "streamUrl": "http://localhost:8791/media"
    })
}
