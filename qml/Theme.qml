// Theme — the Colosseum design tokens (one source of truth for the skin).
// Glass is the constant material; GOLD is a SPARING accent (active / focus / progress only) —
// never a fill-everywhere. Instantiate where needed: `Theme { id: theme }`.
// (No qmldir / singleton yet — the concrete token-kit is an open doctrine seam, deliberately TBD.)

import QtQuick

QtObject {
    // Android system text preference (WT-04). Custom UI fonts use logical pixels, so the
    // retained PlatformRuntime fontScale notification (Activity Configuration.fontScale,
    // independent of the startup density/visual zoom) is applied explicitly. Desktop and any
    // host without the Android runtime keep 1, so desktop typography is unchanged.
    readonly property real fontScale: typeof PlatformRuntime !== "undefined" && PlatformRuntime
        && PlatformRuntime.android === true && typeof PlatformRuntime.fontScale === "number"
        && PlatformRuntime.fontScale > 0 ? PlatformRuntime.fontScale : 1
    function fontSize(base) { return Math.round(base * fontScale) }

    // accent — used sparingly: active pill, focus ring, progress, primary CTA
    readonly property color gold: "#f0c44a"
    readonly property color nuvioAccent: "#a3c3ff"

    // ink
    readonly property color ink: "#f7f7f5"
    readonly property color inkDim: "#c9c8d0"
    readonly property color inkDimmer: "#9a99a5"

    // glass material constants
    readonly property color edge: Qt.rgba(1, 1, 1, 0.18)
    readonly property color glassTint: Qt.rgba(1, 1, 1, 0.10)
    readonly property color glassHi: Qt.rgba(1, 1, 1, 0.14)

    // Biblio world wash — the guide HTML's --biblio-top/--bottom navy stage
    // behind book-detail surfaces (pages 12 / 31). A wash, not chrome gold.
    readonly property color biblioWashTop: "#0c0f18"
    readonly property color biblioWashBottom: "#06070b"

    // type
    // Android has no Segoe UI; use bundled Selawik (SIL OFL, metric-compatible, assets/fonts/selawik).
    // Other platforms unchanged. `hud` reuses the same face so the player HUD matches.
    readonly property FontLoader _selawikRegular: FontLoader { source: "../assets/fonts/selawik/Selawik-Regular.ttf" }
    readonly property FontLoader _selawikLight: FontLoader { source: "../assets/fonts/selawik/Selawik-Light.ttf" }
    readonly property FontLoader _selawikSemilight: FontLoader { source: "../assets/fonts/selawik/Selawik-Semilight.ttf" }
    readonly property FontLoader _selawikSemibold: FontLoader { source: "../assets/fonts/selawik/Selawik-Semibold.ttf" }
    readonly property FontLoader _selawikBold: FontLoader { source: "../assets/fonts/selawik/Selawik-Bold.ttf" }
    readonly property string _androidUiFont: Qt.platform.os === "android" && _selawikRegular.name !== "" ? _selawikRegular.name : "Segoe UI"
    readonly property string ui: _androidUiFont
    readonly property string display: "Fraunces"   // editorial serif — bundled at assets/fonts, loaded in Main.qml
    // player HUD face: Segoe UI — Harbor's EFFECTIVE Windows face. Harbor declares Switzer/Inter
    // but ships neither, so its system-ui resolves to Segoe UI on Windows; matching that is the
    // parity target Hemanth approved (2026-07-19 Harbor player polish). A system font: always
    // present, no bundling, and no "<Name> Variable" registration trap. The whole player HUD
    // follows this token. Registration gate: tests/test_hud_font_registration_p0.ps1.
    readonly property string hud: _androidUiFont   // Selawik on Android, Segoe UI elsewhere

    // layout
    readonly property int margin: 54
    readonly property int extensionCardWidth: 208
    readonly property int extensionCardHeight: 182
    readonly property int extensionCardArtHeight: 98

    // Vault library: locked desktop composition, shared by its small view components.
    readonly property color vaultSurface: "#1a1b22"
    readonly property color vaultSurfaceHi: "#2c2f3a"
    readonly property color vaultBackdrop: "#06070b"
    readonly property color vaultScrim: Qt.rgba(0, 0, 0, 0.40)
    readonly property color vaultGoldInk: "#1a1408"
    readonly property color vaultHealthy: "#5fd38a"
    readonly property color vaultVeilTop: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.55)
    readonly property color vaultVeilBottom: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.82)
    readonly property int vaultRadius: 18
    readonly property int vaultGap: 16
    readonly property int vaultPosterWidth: 140
    readonly property int vaultMotionDuration: 220
    readonly property int vaultSidebarWidth: 248
    readonly property int vaultSidebarInset: 16
    readonly property int vaultSidebarTop: 84
    readonly property int vaultDockClearance: 96
    readonly property int vaultContentLeft: 296
    readonly property int vaultContentRight: 40
    readonly property int vaultSheetWidth: 400
    readonly property int vaultRowHeight: 38
    readonly property int vaultControlRadius: 10
    readonly property int vaultSectionGap: 28
    readonly property int vaultSmall: 11
    readonly property int vaultCaption: 12
    readonly property int vaultBody: 14
    readonly property int vaultSectionTitle: 24
    readonly property int vaultPageTitle: 40
}
