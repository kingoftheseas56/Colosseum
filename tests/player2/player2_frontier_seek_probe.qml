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
// WHAT COUNTS AS PASS (hardened after the 2026-07-26 cross-model review, which was right on all
// three counts):
//   * a NEWLY PRESENTED FRAME. Position and state are promises; a presented frame is the picture
//     actually moving. The two dead runs of 2026-07-26 held a plausible state and a plausible
//     position while presenting nothing, which is exactly what a position-only assertion misses.
//   * WITHIN ONE SECOND of the press. Not five. The requirement is that the press is answered now.
//   * a NON-ZERO EXIT on every failure path, so a runner can detect it. This probe used to print
//     FAIL and exit 0 — a gate that cannot fail is decoration.
//
// Drive it with the window fixture, window CLOSED FOREVER (the frontier never opens):
//   powershell -NoProfile -File tests/player2/player2_http_fixture_server.ps1 `
//     -File artifacts/streamclip.mp4 -Port 8791 -Mode window -WindowBytes 25165824
// then, with COLOSSEUM_PLAYER2=1 and QSG_NO_VSYNC=1 (QML timers stall without it once P2 plays):
//   native\build-msvc\colosseum.exe tests\player2\player2_frontier_seek_probe.qml
// Or just run the gate, which owns the fixture and repeats the run:
//   powershell -NoProfile -File tests/player2/player2_frontier_seek_gate.ps1 -Runs 5
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    title: "Player 2 frontier-seek probe"

    // Inside the served window, and far behind where the picture freezes.
    property real seekTarget: 3.0
    property int ticks: 0
    property real lastPos: -1
    property int stillTicks: 0
    property bool played: false
    property bool seeked: false
    property real stallPos: -1
    property int presentedAtPress: -1
    property double seekPressedAtMs: 0
    property bool finished: false
    // THE requirement, in one number.
    property int budgetMs: 1000

    Player2Page { id: page; anchors.fill: parent }

    function finish(pass, message) {
        if (probe.finished)
            return
        probe.finished = true
        console.log("FRONTIER SEEK PROBE: " + (pass ? "PASS" : "FAIL") + " " + message)
        console.log("FRONTIER SEEK RESULT: " + (pass ? "PASS" : "FAIL"))
        Qt.callLater(function() { Qt.exit(pass ? 0 : 1) })
    }

    Timer {
        interval: 100; repeat: true; running: true
        onTriggered: {
            if (probe.finished)
                return
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
                if (probe.ticks > 900)
                    probe.finish(false, "NOPLAY — never started playing (presented=" + presented
                                        + " state=" + state + ")")
                return
            }

            // --- 2. wait for the frontier: the PICTURE must freeze, not just the transport flag ----
            if (!probe.seeked) {
                if (Math.abs(pos - probe.lastPos) < 0.02)
                    probe.stillTicks += 1
                else
                    probe.stillTicks = 0
                probe.lastPos = pos
                // The transport's stall flag fires as soon as the read parks, which is a whole
                // read-ahead before the picture runs dry. Waiting for the buffer to actually empty
                // is what puts the demux thread in the parked state this probe exists to interrupt.
                if (probe.stillTicks >= 30 && pos > probe.seekTarget + 3.0) {
                    probe.seeked = true
                    probe.stallPos = pos
                    probe.presentedAtPress = presented
                    probe.seekPressedAtMs = Date.now()
                    console.log("FRONTIER SEEK PROBE: picture frozen at pos=" + pos.toFixed(2)
                                + " presented=" + presented + " stalled=" + stalled
                                + " — pressing seek to " + probe.seekTarget)
                    page.sessionSeek(probe.seekTarget)
                    return
                }
                if (probe.ticks > 1800)
                    probe.finish(false, "NOSTALL — never reached the frontier (pos="
                                        + pos.toFixed(2) + ")")
                return
            }

            // --- 3. the seek must be ANSWERED, and the PICTURE must move --------------------------
            var elapsed = Date.now() - probe.seekPressedAtMs
            var newFrames = presented - probe.presentedAtPress
            var movedBack = pos < probe.stallPos - 5.0
            if (newFrames > 0 && movedBack && state === 3) {
                probe.finish(elapsed <= probe.budgetMs,
                             (elapsed <= probe.budgetMs ? "" : "TOO SLOW — ")
                             + "picture moving again " + elapsed + "ms after the press (budget "
                             + probe.budgetMs + "ms), " + newFrames + " new frames, from frozen pos="
                             + probe.stallPos.toFixed(2) + " to pos=" + pos.toFixed(2))
                return
            }
            if (probe.ticks % 20 === 0)
                console.log("FRONTIER SEEK PROBE: waiting " + elapsed + "ms state=" + state
                            + " stalled=" + stalled + " pos=" + pos.toFixed(2)
                            + " newFrames=" + newFrames
                            + " decoded=" + Number((d && d.decoded) || 0))
            // Deliberately much longer than the budget: a run that is merely SLOW must be reported
            // as slow with its real number, not as a hang.
            if (elapsed > 150000)
                probe.finish(false, "NEVER — the seek was never answered (" + elapsed + "ms, "
                                    + newFrames + " new frames, state=" + state + ", pos="
                                    + pos.toFixed(2) + ")")
        }
    }

    Component.onCompleted: page.playRemoteUrl({
        "id": "probe:frontier-seek", "title": "frontier seek probe",
        "streamUrl": "http://localhost:8791/media"
    })
}
