import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// T2d, end to end: A SEEK MUST BE ANSWERED WHILE THE STREAM IS STALLED AT THE DOWNLOAD FRONTIER.
//
// The defect this pins (measured 2026-07-25): when a torrent stops delivering, the demux thread
// parks inside HttpMediaSource::read(), and that park is the ONLY thread that services transport
// commands. So a seek was enqueued to a loop nobody was running — it sat there until the source
// went terminal ~110 s later. What the viewer saw: press seek, nothing happens, the player dies.
//
// The scenario is the honest one: the viewer is watching, the download frontier catches up with
// playback, and he drags the bar BACK to a part he has already downloaded. Those bytes are on the
// origin's disk, so the only thing that can make this slow is us.
//
// Drive it with the window fixture, window CLOSED FOREVER (the frontier never opens):
//   powershell -NoProfile -File tests/player2/player2_http_fixture_server.ps1 `
//     -File artifacts/streamclip.mp4 -Port 8791 -Mode window -WindowBytes 8388608
// then, with COLOSSEUM_PLAYER2=1 and QSG_NO_VSYNC=1 (QML timers stall without it once P2 plays):
//   native\build-msvc\colosseum.exe tests\player2\player2_frontier_seek_probe.qml
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    title: "Player 2 frontier-seek probe"

    // Inside the served window, and FAR behind where the picture freezes, so "it landed" cannot be
    // satisfied by the position it was already sitting at. The read-ahead reaches the frontier long
    // before the picture does (it runs a whole ring ahead), so the wait below is for the PICTURE.
    property real seekTarget: 3.0
    property int ticks: 0
    property real lastPos: -1
    property int stillTicks: 0
    property bool played: false
    property bool seeked: false
    property real stallPos: -1
    property double seekPressedAtMs: 0
    property bool landed: false
    // The whole point of the probe: pre-fix this takes the stall budget (~110 s) or never lands.
    property int budgetMs: 5000

    Player2Page { id: page; anchors.fill: parent }

    Timer {
        interval: 250; repeat: true; running: true
        onTriggered: {
            probe.ticks += 1
            var d = page.diagnosticsSnapshot()
            var presented = Number((d && d.presented) || 0)
            var state = page.sessionState()
            var pos = page.sessionPosition()
            var stalled = page.sessionNetworkStalled()

            // --- 1. real playback first -----------------------------------------------------------
            if (!probe.played) {
                if (presented > 25 && pos > 0.5) {
                    probe.played = true
                    console.log("FRONTIER SEEK PROBE: playing presented=" + presented
                                + " pos=" + pos.toFixed(2))
                }
                if (probe.ticks > 240) {
                    console.log("FRONTIER SEEK PROBE: FAIL-NOPLAY presented=" + presented
                                + " state=" + state)
                    Qt.callLater(function() { Qt.quit() })
                }
                return
            }

            // --- 2. wait for the frontier: the origin holds the connection and says nothing --------
            if (!probe.seeked) {
                if (Math.abs(pos - probe.lastPos) < 0.02)
                    probe.stillTicks += 1
                else
                    probe.stillTicks = 0
                probe.lastPos = pos
                // The PICTURE must have frozen — the transport's stall flag fires as soon as the
                // read parks, which is a whole read-ahead earlier, while playback is still fine.
                // Waiting for the buffer to actually run dry is what puts the demux thread in the
                // parked state this probe exists to interrupt, and it puts the seek target far
                // enough behind that landing on it cannot be confused with standing still.
                if (probe.stillTicks >= 12 && pos > probe.seekTarget + 3.0) {
                    probe.seeked = true
                    probe.stallPos = pos
                    probe.seekPressedAtMs = Date.now()
                    console.log("FRONTIER SEEK PROBE: picture frozen at pos=" + pos.toFixed(2)
                                + " stalled=" + stalled + " — pressing seek to " + probe.seekTarget)
                    page.sessionSeek(probe.seekTarget)
                    return
                }
                if (probe.ticks % 4 === 0)
                    console.log("FRONTIER SEEK PROBE: pre-seek tick=" + probe.ticks + " pos="
                                + pos.toFixed(2) + " still=" + probe.stillTicks + " state=" + state
                                + " stalled=" + stalled + " presented=" + presented)
                if (probe.ticks > 480) {
                    console.log("FRONTIER SEEK PROBE: FAIL-NOSTALL never reached the frontier pos="
                                + pos.toFixed(2))
                    Qt.callLater(function() { Qt.quit() })
                }
                return
            }

            // --- 3. the seek must be ANSWERED, not queued behind a dead read ----------------------
            var elapsed = Date.now() - probe.seekPressedAtMs
            // Landed = the film is PLAYING again from somewhere well behind the frozen position.
            // Deliberately not "within X of the target": this engine holds the clock after a seek
            // until the audio is genuinely audible (loudnorm refills ~3 s), so the first visible
            // position is a few seconds past the target — measured, not assumed. Pinning the exact
            // target would fail a player that is working correctly in front of him.
            if (!probe.landed && state === 3 && pos < probe.stallPos - 5.0) {
                probe.landed = true
                console.log("FRONTIER SEEK PROBE: " + (elapsed <= probe.budgetMs ? "PASS" : "FAIL-SLOW")
                            + " seek answered in " + elapsed + "ms (budget " + probe.budgetMs
                            + "ms) from frozen pos=" + probe.stallPos.toFixed(2)
                            + " to pos=" + pos.toFixed(2) + " state=" + state)
                Qt.callLater(function() { Qt.quit() })
                return
            }
            // Which stage is stuck matters more than the fact that it is: decoded growing with
            // presented flat is a presentation/clock hold, both flat is a decode or feed problem.
            if (probe.ticks % 8 === 0)
                console.log("FRONTIER SEEK PROBE: waiting " + elapsed + "ms state=" + state
                            + " stalled=" + stalled + " pos=" + pos.toFixed(2)
                            + " decoded=" + Number((d && d.decoded) || 0)
                            + " submitted=" + Number((d && d.submitted) || 0)
                            + " presented=" + presented)
            if (elapsed > 150000) {
                console.log("FRONTIER SEEK PROBE: FAIL-NEVER the seek was never answered ("
                            + elapsed + "ms) state=" + state + " pos=" + pos.toFixed(2))
                Qt.callLater(function() { Qt.quit() })
            }
        }
    }

    Component.onCompleted: page.playRemoteUrl({
        "id": "probe:frontier-seek", "title": "frontier seek probe",
        "streamUrl": "http://localhost:8791/media"
    })
}
