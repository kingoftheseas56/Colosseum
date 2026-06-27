// PortraitTile — a SOLID portrait cover tile (content art, NOT glass).
// Shared by the home trending rows and the world page's Continue row / ranked widgets.
// Optional resume progress bar (gold — the only accent). Hover = gold border, never scale
// (scale gets clipped by the row's scroll clip).

import QtQuick

Rectangle {
    id: tile

    property string caption
    property color c1: "#444"
    property color c2: "#111"
    property real progress: -1        // < 0 → no progress bar
    property url cover: ""            // remote cover art; the c1→c2 tint shows through until it loads (or if it fails)
    signal clicked()

    width: 132; height: 196; radius: 12; clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: tile.c1 }
        GradientStop { position: 1; color: tile.c2 }
    }
    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)

    Theme { id: theme }

    // remote cover art — async + cached; the gradient tint below stands in while it loads and
    // stays put if the fetch fails, so a tile is never blank. This is the Qt analogue of the
    // Electron <img> + progressive AniList/iTunes covers. sourceSize caps decode memory.
    Image {
        id: art
        anchors.fill: parent
        source: tile.cover
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: 264; sourceSize.height: 392
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }

    Text {
        text: tile.caption; color: theme.ink
        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 9
        anchors.bottomMargin: tile.progress >= 0 ? 14 : 9
        wrapMode: Text.WordWrap
        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.85)
    }

    // resume progress (gold) — only when progress >= 0
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

    // hover highlight — gold border, no scale
    Rectangle {
        anchors.fill: parent; radius: parent.radius
        color: ma.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
        border.width: 2
        border.color: ma.containsMouse ? theme.gold : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    MouseArea {
        id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        onClicked: tile.clicked()
    }
}
