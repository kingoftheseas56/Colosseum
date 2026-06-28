// TheatreCatalogRow - Harbor-style horizontal catalog rail using Colosseum portrait tiles.

import QtQuick

pragma ComponentBehavior: Bound

Column {
    id: rail

    property string title: ""
    property string sub: ""
    property var items: []
    property bool ranked: false
    signal itemRequested(var item)

    width: parent ? parent.width : 900
    spacing: 14
    visible: items.length > 0

    Theme { id: theme }

    WidgetHeader {
        width: parent.width
        title: rail.title
        sub: rail.sub
        moreLabel: "View all"
        navigable: false
    }

    Item {
        width: parent.width
        height: rail.ranked ? 212 : 214

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: row.width
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: row
                spacing: rail.ranked ? 30 : 18
                leftPadding: 6
                rightPadding: 26

                Repeater {
                    model: rail.items
                    delegate: Item {
                        id: cell
                        required property var modelData
                        required property int index

                        width: rail.ranked ? rankNum.width + cover.width - 32 : 132
                        height: rail.ranked ? 212 : 214

                        Text {
                            id: rankNum
                            visible: rail.ranked
                            text: cell.index + 1
                            color: Qt.rgba(1, 1, 1, 0.16)
                            font.family: theme.display
                            font.bold: true
                            font.pixelSize: 132
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: -8
                        }

                        PortraitTile {
                            id: cover
                            width: 132
                            height: 196
                            anchors.left: rail.ranked ? rankNum.right : parent.left
                            anchors.leftMargin: rail.ranked ? -32 : 0
                            anchors.bottom: parent.bottom
                            caption: cell.modelData.caption !== undefined ? cell.modelData.caption : (cell.modelData.title || "")
                            cover: cell.modelData.cover !== undefined ? cell.modelData.cover : ""
                            c1: cell.modelData.c1 !== undefined ? cell.modelData.c1 : "#444"
                            c2: cell.modelData.c2 !== undefined ? cell.modelData.c2 : "#111"
                            onClicked: rail.itemRequested(cell.modelData)
                        }
                    }
                }
            }
        }
    }
}
