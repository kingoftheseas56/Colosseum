// Theme — the Colosseum design tokens (one source of truth for the skin).
// Glass is the constant material; GOLD is a SPARING accent (active / focus / progress only) —
// never a fill-everywhere. Instantiate where needed: `Theme { id: theme }`.
// (No qmldir / singleton yet — the concrete token-kit is an open doctrine seam, deliberately TBD.)

import QtQuick

QtObject {
    // accent — used sparingly: active pill, focus ring, progress, primary CTA
    readonly property color gold: "#f0c44a"

    // ink
    readonly property color ink: "#f7f7f5"
    readonly property color inkDim: "#c9c8d0"
    readonly property color inkDimmer: "#9a99a5"

    // glass material constants
    readonly property color edge: Qt.rgba(1, 1, 1, 0.18)
    readonly property color glassTint: Qt.rgba(1, 1, 1, 0.10)
    readonly property color glassHi: Qt.rgba(1, 1, 1, 0.14)

    // type
    readonly property string ui: "Segoe UI"
    readonly property string display: "Fraunces"   // editorial serif — bundled at assets/fonts, loaded in Main.qml
    // player HUD face: Switzer (Harbor-parity), a crafted sans that replaces the generic system
    // font inside the video player. Bundled at assets/fonts, loaded in Main.qml. Flip this to
    // "Inter" (statics genuinely bundled as of 2026-07-08 — this comment used to lie) and the
    // whole HUD follows. Bundle STATICS only: a variable TTF registers as "<Name> Variable", so
    // the plain family name silently falls back to Tahoma (probe-proven; QML never errors on an
    // unknown family — the gate is tests/test_hud_font_registration_p0.ps1).
    readonly property string hud: "Inter"

    // layout
    readonly property int margin: 54
}
