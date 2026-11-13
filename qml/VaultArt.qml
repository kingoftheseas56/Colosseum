import QtQuick

// Shared Vault artwork: bounded existing art plus an intentional typographic fallback.
Item {
    id: art
    property var row: ({})
    property bool landscape: false
    property real radius: 10
    property bool hovered: false
    readonly property string title: String(row.title || "Untitled")
    Theme { id: theme }
    RoundedPosterImage {
        id: image
        anchors.fill: parent
        radius: art.radius
        hovered: art.hovered
        revealDuration: theme.vaultMotionDuration
        sources: art.landscape
            ? [art.row.backdrop || art.row.art || art.row.cover || art.row.poster || "", art.row.cover || art.row.poster || ""]
            : [art.row.cover || art.row.poster || "", art.row.backdrop || art.row.thumbnail || ""]
    }
    Rectangle {
        anchors.fill: parent
        radius: art.radius
        visible: !image.ready
        gradient: Gradient {
            GradientStop { position: 0; color: theme.vaultSurfaceHi }
            GradientStop { position: 1; color: theme.vaultBackdrop }
        }
        border.color: theme.edge
        Rectangle { x: 16; y: 18; width: 22; height: 2; color: theme.gold }
        Text {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Math.min(16, art.width * 0.11) }
            text: art.title; color: theme.ink; font.family: theme.display
            font.pixelSize: art.landscape ? 27 : Math.max(12, Math.min(23, art.width * 0.15)); wrapMode: Text.WordWrap
            maximumLineCount: 4; elide: Text.ElideRight
        }
        Text {
            anchors { left: parent.left; bottom: parent.bottom; margins: Math.min(16, art.width * 0.11) }
            text: String(art.row.kind || "YOUR LIBRARY").toUpperCase()
            font.family: theme.ui; font.pixelSize: art.width < 100 ? 7 : 9; font.letterSpacing: art.width < 100 ? 1 : 2; color: theme.inkDimmer
        }
    }
}
