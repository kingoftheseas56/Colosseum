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
    readonly property string display: "Georgia"   // editorial serif (Fraunces is the target; bundle later)

    // layout
    readonly property int margin: 54
}
