import QtQuick
import QtQuick.Window
import Colosseum.Player 1.0

// Bare QtQuick/MpvItem isolation probe for the 2026-07-28 mpv zero-drop experiment.
//
// ONE visible black Window + ONE MpvItem, loading the Tenet file on completion. No PlayerPage,
// no qml/Main.qml full runtime, no Player 2. Purpose: separate "QtQuick/MpvItem presentation
// alone" from "the full Colosseum runtime" as the cause of the periodic clustered hitch
// (full Colosseum: +469 output drops / 300s with a visible 3-4-frame hitch every 4-8s; standalone
// mpv forced to the SAME d3d11va-copy + display-resample + interpolation policy: +1 / 300s).
//
// MpvItem's constructor already applies the approved policy (hwdec=auto-safe,
// video-sync=display-resample, interpolation=yes), matching the standalone-mpv and full-app
// runs. The C++ drop probe (env COLOSSEUM_MPV_DROP_PROBE=warmup,measure, set by the gate) arms
// on fileLoaded, samples the mpv counters across the warm-up+measure window, emits
// "MPV_DROP_PROBE RESULT {json}" to stderr, and quits the process. So this QML only creates the
// item and loads Tenet; it does not touch counters or exit logic.
//
// Run via tests/mpv_zero_drop_gate.ps1 -QmlEntry tests\mpv_qtquick_tenet_probe.qml
Window {
    id: probe
    // Same 1280x720 surface as the Player 2 harness / mpv baseline probe, so no contender is
    // asked to paint a different pixel budget.
    width: 1280
    height: 720
    visible: true
    color: "black"
    title: "Colosseum bare QtQuick MpvItem probe - Tenet"

    property string clip: "C:/Users/Suprabha/Downloads/Colosseum/Tenet - 20260726_184029.mp4"

    MpvItem {
        id: mpv
        anchors.fill: parent
    }

    Component.onCompleted: {
        mpv.loadFile(probe.clip)
        console.log("MPV QTQUICK TENET PROBE: playing " + probe.clip)
    }

    // Sanity heartbeat only (the C++ probe owns the authoritative telemetry). A pass that
    // silently failed to play must never be mistaken for a smooth one.
    Timer {
        interval: 5000
        repeat: true
        running: true
        onTriggered: console.log("MPV QTQUICK TENET PROBE: pos=" + mpv.position.toFixed(1)
                                 + " dur=" + mpv.duration.toFixed(1)
                                 + " paused=" + mpv.pause)
    }
}
