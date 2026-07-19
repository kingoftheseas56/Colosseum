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
    readonly property color edgeSoft: Qt.rgba(1, 1, 1, 0.09)
    readonly property color glassTint: Qt.rgba(1, 1, 1, 0.10)
    readonly property color glassHi: Qt.rgba(1, 1, 1, 0.14)
    readonly property color glassSheet: Qt.rgba(14 / 255, 16 / 255, 22 / 255, 0.42)   // the AF2 Home board sheet

    // per-world identity tint — a FAINT cue only (a dot, a section tag); never a surface
    // fill. Gold stays the only real accent. (AF2 Home redesign, 2026-07-19.)
    readonly property color tintTheatre: "#7aa2ff"
    readonly property color tintTankoban: "#f0a24a"
    readonly property color tintBiblio: "#9d8cff"

    // type
    readonly property string ui: "Segoe UI"
    readonly property string display: "Fraunces"   // editorial serif — bundled at assets/fonts, loaded in Main.qml
    // player HUD face: Segoe UI — Harbor's EFFECTIVE Windows face. Harbor declares Switzer/Inter
    // but ships neither, so its system-ui resolves to Segoe UI on Windows; matching that is the
    // parity target Hemanth approved (2026-07-19 Harbor player polish). A system font: always
    // present, no bundling, and no "<Name> Variable" registration trap. The whole player HUD
    // follows this token. Registration gate: tests/test_hud_font_registration_p0.ps1.
    readonly property string hud: "Segoe UI"

    // layout
    readonly property int margin: 54

    // AF2 cadence (Includes_Constants.xml, scaled from the skin's 1080 grid to our canvas)
    readonly property int homePad: 64          // view_pad=80 -> page gutter
    readonly property int rowH: 300            // view_row=510 -> landscape rail height rhythm
    // sans display face for the AF2 Home headers/hero (Figtree, OFL — AF2 ships it, Switzer-adjacent)
    readonly property string displaySans: "Figtree"
}
