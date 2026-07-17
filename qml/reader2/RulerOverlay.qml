// RulerOverlay.qml — the reading RULER (TASK 11): a horizontal focus band with dimmed
// regions above and below it, so the eye is drawn to the line you're reading. Driven by the
// Appearance panel's ruler CONTROLS (rulerOn / rulerHeightPx / rulerDimPct / rulerYPct).
// Pixel contract: the chrome mock's `.ruler` / `.band` (agents/colosseum-book-reader-chrome-mock.html).
//
// HARD CONSTRAINT — it must NEVER block text selection. This overlay sits OVER the
// WebEngineView (the paper), and a full-cover interactive MouseArea would re-block the
// press/drag the paper needs to select text — the exact bug Task 9 fixed. So this layer is
// PURE PAINT: plain Rectangles, ZERO MouseAreas / handlers anywhere. Every press/drag falls
// straight through to the paper. Reposition is done WITHOUT any page interaction — a "Band
// position" slider in the Appearance panel drives `yPct` (no grip over the reading column),
// which is why there is no interactive element here at all.
//
// Geometry is the pure Reader2Logic.rulerGeometry() (band-top + scrim heights from
// yPct/heightPx/overlayHeight), so it is proven headless and stays clamped on-screen.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: overlay

    // ---- inputs (bound from ReaderShell's shell.appearance) ----
    property bool on: false
    property int heightPx: 92
    property int dimPct: 42
    property int yPct: 40

    visible: on

    // pure band/scrim layout; re-derives whenever a prop or the overlay size changes.
    readonly property var geo: L.rulerGeometry(overlay.yPct, overlay.heightPx, overlay.height)
    // scrim opacity from the "Dim outside" control (0..100 → 0..1).
    readonly property real scrimAlpha: Math.max(0, Math.min(1, overlay.dimPct / 100))

    // ---- dim scrim ABOVE the band (mock .ruler::before) ----
    Rectangle {
        x: 0; y: 0
        width: parent.width
        height: overlay.geo.topScrimH
        color: Qt.rgba(6 / 255, 6 / 255, 8 / 255, overlay.scrimAlpha)
    }

    // ---- dim scrim BELOW the band (mock .ruler::after) ----
    Rectangle {
        x: 0
        y: overlay.geo.bandTop + overlay.geo.bandHeight
        width: parent.width
        height: overlay.geo.botScrimH
        color: Qt.rgba(6 / 255, 6 / 255, 8 / 255, overlay.scrimAlpha)
    }

    // ---- the focus band: a faint warm tint + hairline top/bottom rules (mock .band) ----
    Rectangle {
        x: 0
        y: overlay.geo.bandTop
        width: parent.width
        height: overlay.geo.bandHeight
        color: Qt.rgba(255 / 255, 236 / 255, 170 / 255, 0.05)

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.rgba(255 / 255, 236 / 255, 170 / 255, 0.10)
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.rgba(255 / 255, 236 / 255, 170 / 255, 0.10)
        }
    }
}
