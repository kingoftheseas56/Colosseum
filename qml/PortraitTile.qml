// PortraitTile — a SOLID portrait cover tile (content art, NOT glass).
// Shared by the home trending rows and the world page's Continue row / ranked widgets (TrendingTop10,
// used by both the Biblio and Tankoban worlds). Optional resume progress bar (gold — the only accent).
//
// Gallery finish (Catalogue Poster & Shelf Polish): the cover is rendered through ONE rounded
// MultiEffect mask so the 12px corners are a GENUINE crop (never a rectangular clip that lets art
// paint over the border), with two CHEAP flat rounded depth plates behind it (no GPU blur) and the
// shared inset edge — 1px white 8% at rest, 2px soft gold on hover. Geometry reads the frozen
// CatalogueVisualMetrics gallery tokens so every portrait shelf renders as one family with Theatre
// (148x222) and with ContinueTile's world variant. The tile keeps its own function: the c1→c2 tint
// that stands in until a remote cover loads (and stays on failure), the optional gold progress bar,
// and the whole-tile hover + click. Hover = gold edge, never scale (scale gets clipped by the row's
// scroll clip).

import QtQuick
import QtQuick.Effects
import "CatalogueVisualMetrics.js" as Metrics

Item {
    id: tile

    property string caption
    property color c1: "#444"
    property color c2: "#111"
    property real progress: -1        // < 0 → no progress bar
    property real posterWidth: Metrics.gallery.posterWidth // opt-in larger universe shelves; default unchanged
    property url cover: ""            // remote cover art; the c1→c2 tint shows through until it loads (or if it fails)
    signal clicked()

    // gallery geometry — read from the frozen tokens; never a local copy of these numbers.
    readonly property real _r: Metrics.gallery.posterRadius
    width: tile.posterWidth
    height: Math.round(tile.posterWidth * Metrics.gallery.posterRatio)

    Theme { id: theme }

    // ── two cheap offset depth plates behind the art (flat rounded rects; no blur, no FBO) —
    //    deepen slightly on hover, matching the gallery posters + ContinueTile world variant. ──
    Rectangle {
        x: 0; y: 3; width: tile.width; height: tile.height; radius: tile._r + 1
        color: Qt.rgba(0, 0, 0, ma.containsMouse ? 0.42 : 0.28)
        Behavior on color { ColorAnimation { duration: 220 } }
    }
    Rectangle {
        x: -2; y: ma.containsMouse ? 11 : 7; width: tile.width + 4; height: tile.height; radius: tile._r + 3
        color: Qt.rgba(0, 0, 0, ma.containsMouse ? 0.20 : 0.10)
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 220 } }
    }

    // ── the in-tile content (gradient tint + remote cover + caption + progress) rendered ONLY
    //    through the single rounded MultiEffect mask, so the corners are genuinely cropped. The old
    //    rectangular clip let cover art paint over the border; this never can. ──
    Item {
        id: content
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: maskShape
            maskThresholdMin: 0.5      // crisp 50% cutoff on the AA'd rounded-rect mask texture
        }

        // c1→c2 tint — visible instantly, stands in while the remote cover loads, stays on failure.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: tile.c1 }
                GradientStop { position: 1; color: tile.c2 }
            }
        }
        // remote cover art — async + cached; decode-capped (≤2× the rendered poster) so memory is
        // bounded. Fades in over the tint on ready; one clean retry on error, then the tint stays.
        Image {
            id: art
            property bool retried: false
            anchors.fill: parent
            source: tile.cover
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true               // mipmap only on the BOUNDED decoded image, never an unbounded original
            sourceSize.width: Math.ceil(tile.width * 2)
            sourceSize.height: Math.ceil(tile.height * 2)
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
            onStatusChanged: if (status === Image.Error && !retried && ("" + tile.cover).length) {
                retried = true
                var s = tile.cover; source = ""; source = s   // one clean retry, then the tint stays
            }
        }
        // caption — inside the tile, readable over ANY cover art thanks to the outline
        Text {
            text: tile.caption; color: theme.ink
            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: 9
            anchors.bottomMargin: tile.progress >= 0 ? 14 : 9
            wrapMode: Text.WordWrap
            style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.85)
        }
        // resume progress (gold) — only when progress >= 0; full bottom edge, rendered inside the mask
        Rectangle {
            visible: tile.progress >= 0
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 4
            color: Qt.rgba(1, 1, 1, 0.2)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, tile.progress)); height: parent.height
                color: theme.gold
            }
        }
    }

    // stable rounded mask source (no animation) — a texture provider, not drawn directly.
    Item {
        id: maskShape
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle { anchors.fill: parent; radius: tile._r; color: "black" }
    }

    // resting inset edge: 1px white 8%; becomes 2px soft gold on hover (the shared gallery edge).
    Rectangle {
        anchors.fill: parent
        radius: tile._r; color: "transparent"
        border.width: ma.containsMouse ? 2 : 1
        border.color: ma.containsMouse ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
                                       : Qt.rgba(1, 1, 1, 0.08)
        Behavior on border.color { ColorAnimation { duration: 220 } }
    }

    MouseArea {
        id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        onClicked: tile.clicked()
    }
}
