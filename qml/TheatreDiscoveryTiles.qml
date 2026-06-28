// TheatreDiscoveryTiles - Harbor-like genre/language/collection tile block.

import QtQuick

pragma ComponentBehavior: Bound

Column {
    id: block

    property string title: ""
    property var tiles: []
    property int columns: 4
    signal tileClicked(var tile)

    width: parent ? parent.width : 900
    spacing: 14
    visible: tiles.length > 0

    Theme { id: theme }

    WidgetHeader {
        width: parent.width
        title: block.title
        sub: ""
        navigable: false
    }

    Grid {
        id: grid
        width: parent.width
        columns: block.columns
        columnSpacing: 14
        rowSpacing: 14
        readonly property real cellW: (width - (columns - 1) * columnSpacing) / columns

        Repeater {
            model: block.tiles
            delegate: Rectangle {
                id: tile
                required property var modelData

                width: grid.cellW
                height: 116
                radius: 18
                clip: true
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: tile.modelData.c1 || "#263241" }
                    GradientStop { position: 1.0; color: tile.modelData.c2 || "#0c1118" }
                }
                border.width: 1
                border.color: ma.containsMouse ? theme.gold : Qt.rgba(1, 1, 1, 0.12)

                Text {
                    text: tile.modelData.ghost || ""
                    color: Qt.rgba(1, 1, 1, 0.10)
                    font.family: theme.display
                    font.pixelSize: 92
                    font.weight: Font.Bold
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.top: parent.top
                    anchors.topMargin: -16
                }
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 18
                    text: tile.modelData.title || ""
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: block.tileClicked(tile.modelData)
                }
            }
        }
    }
}
