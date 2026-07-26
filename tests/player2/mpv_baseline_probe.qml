import QtQuick
import QtQuick.Window
import Colosseum.Player 1.0

// The mpv side of the efficiency comparison: ONE video item playing the clip in a plain window, with
// no app chrome, matching what the Player 2 harness does. This exists because the gate's original
// wiring passed the clip to colosseum.exe as a command-line argument - but that argument selects a
// QML entry point, not a movie, so "production" would have measured the app sitting on its home
// screen while Player 2 decoded video. That is not a comparison, it is a fabricated advantage.
//
// Set COLOSSEUM_ABBA_IDLE=1 to hold the same window open WITHOUT playing. Subtracting that pass from
// the playing pass is what separates mpv's real cost from the app shell that has to host it.
//
// Run: colosseum.exe tests/player2/mpv_baseline_probe.qml   (OpenGL boot - mpv requires it)
Window {
    id: probe
    // Same surface as the Player 2 harness window, so neither contender is asked to paint more pixels.
    width: 1280
    height: 720
    visible: true
    color: "black"
    title: "mpv efficiency baseline"

    property string clip: "C:/Users/Suprabha/Downloads/Colosseum/The Wire - S4E10 - Misgivings - 20260720_175049.mp4"
    // Set by the ABBA runner via COLOSSEUM_ABBA_IDLE to hold the window without playing.
    property bool idleMode: (typeof DevAbbaIdle !== "undefined") ? DevAbbaIdle : false

    MpvItem {
        id: mpv
        anchors.fill: parent
    }

    Component.onCompleted: {
        if (probe.idleMode) {
            console.log("MPV BASELINE: idle window only (no playback)")
            return
        }
        mpv.loadFile(probe.clip)
        console.log("MPV BASELINE: playing " + probe.clip)
    }

    // Report the same shape of truth the Player 2 probe does, so a pass that silently failed to play
    // is never mistaken for an efficient one.
    Timer {
        interval: 5000
        repeat: true
        running: !probe.idleMode
        onTriggered: console.log("MPV BASELINE: pos=" + mpv.position.toFixed(1)
                                 + " dur=" + mpv.duration.toFixed(1)
                                 + " paused=" + mpv.pause)
    }
}
