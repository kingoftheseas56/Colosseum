import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// STREAM seek probe for the PRODUCTION page. player2_seek_probe.qml already proves a seek over a
// LOCAL file is clean, so the defect Hemanth hit ("if i seek forward or backward the video player
// closes") is specific to seeking a PROGRESSIVE origin — a torrent whose bytes past the download
// frontier have not arrived yet.
//
// Drive it with tests/player2/player2_http_fixture_server.ps1 -Mode window, which reproduces the
// Stremio EngineFS sidecar exactly (206 at the exact offset, then the body stalls at the frontier):
//   powershell -NoProfile -File tests/player2/player2_http_fixture_server.ps1 `
//     -File artifacts/streamclip.mp4 -Port 8791 -Mode window -WindowBytes 4194304
//
// The probe plays inside the served window, then seeks WELL past it and reports the session state,
// the position, and page.errorText — the engine's REAL message, which is the root-cause record.
//
// It also reports session.networkStalled and the page's derived status text every tick, because the
// engine waiting on purpose is only half the fix: a legitimate 30s wait that renders as a dead
// player is the ORIGINAL complaint ("if i seek forward or backward the video player closes"). Run
// the fixture with -WindowOpenAfterSec N to watch the flag go true for the wait and false when the
// bytes land.
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    title: "Player 2 stream seek probe"

    property int ticks: 0
    property bool seeked: false
    property real seekTarget: 60.0
    // Set if the full-screen loading surface ever came up after the seek. It must not.
    property bool loaderRaised: false

    Player2Page { id: page; anchors.fill: parent }

    Timer {
        interval: 500; repeat: true; running: true
        onTriggered: {
            probe.ticks += 1
            var d = page.diagnosticsSnapshot()
            var presented = Number((d && d.presented) || 0)
            var state = page.sessionState()
            var stalled = page.sessionNetworkStalled()
            var status = page.statusText()
            // Parity guard: the shipped player never raises the loading surface on a seek or a
            // mid-playback buffer (only open/retry/reconnect), so this MUST stay false through the
            // whole stall - the picture stays on screen and the transport line does the talking.
            var loading = page.loadingActive()
            // loadingActive() is `_starting || errored`, and the error surface legitimately uses the
            // same screen. Only a loader raised WITHOUT an error is the parity violation this field
            // is meant to catch - otherwise the bytes-never-arrive run reports a false positive.
            if (probe.seeked && loading && page.errorText.length === 0)
                probe.loaderRaised = true

            // Every tick after the seek, so the wait is a visible RUN of lines, not a single sample.
            if (probe.seeked)
                console.log("STREAM SEEK PROBE: tick=" + probe.ticks + " state=" + state
                            + " networkStalled=" + stalled + " status=[" + status + "]"
                            + " _starting=" + loading
                            + " pos=" + page.sessionPosition().toFixed(2))

            if (!probe.seeked && presented > 25) {
                probe.seeked = true
                console.log("STREAM SEEK PROBE: pre-seek OK presented=" + presented + " state=" + state
                            + " pos=" + page.sessionPosition() + " dur=" + page.sessionDuration())
                console.log("STREAM SEEK PROBE: seeking to " + probe.seekTarget + "s (past the served window)")
                page.sessionSeek(probe.seekTarget)
                return
            }
            if (probe.seeked && probe.ticks % 4 === 0) {
                console.log("STREAM SEEK PROBE: post-seek state=" + state + " presented=" + presented
                            + " pos=" + page.sessionPosition()
                            + " errorText=[" + page.errorText + "]")
            }
            if (!probe.seeked && probe.ticks % 8 === 0) {
                console.log("STREAM SEEK PROBE: warmup state=" + state + " presented=" + presented
                            + " pos=" + page.sessionPosition()
                            + " errorText=[" + page.errorText + "]")
            }
            // Finish as soon as the outcome is decided either way: Error (8) is terminal, and
            // Playing at the target is the fix's success condition. Otherwise run out the budget so
            // a HANG is reported as a hang rather than mistaken for either.
            var landed = probe.seeked && state === 3 && page.sessionPosition() >= (probe.seekTarget - 2.0)
                         && !probe.loaderRaised
            var errored = state === 8 || page.errorText.length > 0
            if (landed || errored || probe.ticks > 700) {
                console.log("STREAM SEEK PROBE: " + (landed ? "PASS" : (errored ? "FAIL-ERROR" : "FAIL-HANG"))
                            + " finalState=" + state + " presented=" + presented
                            + " pos=" + page.sessionPosition()
                            + " networkStalled=" + stalled + " status=[" + status + "]"
                            + " loaderRaised=" + probe.loaderRaised
                            + " errorText=[" + page.errorText + "]")
                Qt.callLater(function() { Qt.quit() })
            }
        }
    }

    Connections {
        target: page
        function onBackendRestartRequired(reason) {
            console.log("STREAM SEEK PROBE: session errored -> " + reason)
        }
        function onBackendFallback(reason) {
            console.log("STREAM SEEK PROBE: FALLBACK -> " + reason)
        }
        function onErrorTextChanged() {
            if (page.errorText.length)
                console.log("STREAM SEEK PROBE: errorText SET -> " + page.errorText)
        }
    }

    Component.onCompleted: page.playRemoteUrl({
        "id": "probe:stream", "title": "stream seek probe",
        "streamUrl": "http://localhost:8791/media"
    })
}
