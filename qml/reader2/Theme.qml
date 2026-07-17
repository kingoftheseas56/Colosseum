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

    // slide-in glass panels (left Contents/Bookmarks/Highlights, right Appearance).
    // A hair darker + more opaque than the bars, per the mock's .panel background
    // rgba(13,13,16,.86) — reads as a solid column over the paper, not a thin scrim.
    readonly property color panelBg: Qt.rgba(13 / 255, 13 / 255, 16 / 255, 0.86)
    // faint gold wash behind the CURRENT contents row (mock rgba(240,194,74,.07)).
    readonly property color goldWash: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.07)
    // hover tint on a panel row (mock rgba(255,255,255,.05)).
    readonly property color rowHover: Qt.rgba(1, 1, 1, 0.05)
    // the indented-note left rule (mock rgba(255,255,255,.14)).
    readonly property color noteRule: Qt.rgba(1, 1, 1, 0.14)

    // Audio pane (Task 13) — the mock's inset cards + switch track.
    // card fill (mock .audiohead/.followrow/.transport background rgba(255,255,255,.04)).
    readonly property color cardBg: Qt.rgba(1, 1, 1, 0.04)
    // OFF switch track (mock .switch.off rgba(255,255,255,.16)); ON track is `gold`.
    readonly property color switchTrackOff: Qt.rgba(1, 1, 1, 0.16)
    // the switch knob (mock ::after background #141416 on gold, #… kept dark on both).
    readonly property color switchKnob: "#141416"

    // progress rail
    readonly property color track: Qt.rgba(1, 1, 1, 0.13)
    readonly property color tick: Qt.rgba(1, 1, 1, 0.22)

    // reveal scrims — top/bottom gradients fade FROM this near-black.
    readonly property color scrim: Qt.rgba(8 / 255, 8 / 255, 10 / 255, 1.0)

    // type — UI = Inter (statics bundled), display serif = Fraunces (bundled). Both are
    // loaded by Main.qml in the real app and by Harness.qml in the standalone harness.
    readonly property string ui: "Inter"
    readonly property string display: "Fraunces"

    // reveal idle timeout (ms) — chrome sleeps this long after the cursor leaves the
    // top/bottom edge band (or the book-open/toggle reveal). MANUALLY kept in lock-step
    // with Reader2Logic's REVEAL_IDLE_MS: they are two separate literals because a
    // `.pragma library` (Reader2Logic.js) can't import this singleton, so the reducer
    // can't read this value — change one, change the other. Matches the comic reader's 3s beat.
    readonly property int idleMs: 3000
}
