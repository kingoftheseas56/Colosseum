// Theme.qml — reader2 chrome design tokens (SINGLETON; the one source of truth for
// the fresh reader's glass-over-paper skin). Byte-for-byte the constants from the
// ratified chrome mock (agents/colosseum-book-reader-chrome-mock.html): GOLD is the
// sparing accent (progress fill / knob / active), the ink ramp is white-at-alpha, and
// the bars are the same smoked glass the video-player HUD uses.
//
// Declared `singleton` in qml/reader2/qmldir → referenced as `Theme.gold` from any
// sibling reader2 component (no per-instance copy).
//
// [Agent 2 (Claude), biblio]
pragma Singleton
import QtQuick

QtObject {
    // accent — progress fill, knob, active tick/label. Used SPARINGLY.
    readonly property color gold: "#F0C24A"

    // ink ramp = white at descending alpha (matches the mock's --ink* vars exactly).
    readonly property color ink: "#ffffff"
    readonly property color inkDim: Qt.rgba(1, 1, 1, 0.62)
    readonly property color inkFaint: Qt.rgba(1, 1, 1, 0.40)
    readonly property color inkGhost: Qt.rgba(1, 1, 1, 0.26)
    // title is a hair brighter than inkDim in the mock (rgba(255,255,255,.84)).
    readonly property color inkTitle: Qt.rgba(1, 1, 1, 0.84)

    // glass bars (smoked, like the HUD) + hairline border.
    readonly property color bar: Qt.rgba(16 / 255, 16 / 255, 19 / 255, 0.72)
    readonly property color barBorder: Qt.rgba(1, 1, 1, 0.07)

    // progress rail
    readonly property color track: Qt.rgba(1, 1, 1, 0.13)
    readonly property color tick: Qt.rgba(1, 1, 1, 0.22)

    // reveal scrims — top/bottom gradients fade FROM this near-black.
    readonly property color scrim: Qt.rgba(8 / 255, 8 / 255, 10 / 255, 1.0)

    // type — UI = Inter (statics bundled), display serif = Fraunces (bundled). Both are
    // loaded by Main.qml in the real app and by Harness.qml in the standalone harness.
    readonly property string ui: "Inter"
    readonly property string display: "Fraunces"

    // reveal idle timeout (ms) — chrome sleeps this long after the last pointer move.
    // Mirrors Reader2Logic.revealReducer's threshold; kept here so the chrome's Timer
    // and the pure reducer agree on one number.
    readonly property int idleMs: 1800
}
