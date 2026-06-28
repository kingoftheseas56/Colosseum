// PosterRail - lightweight horizontal Theatre rail with delegate reuse.

import QtQuick

pragma ComponentBehavior: Bound

Column {
    id: rail

    property string title: ""
    property var items: []
    property bool ranked: false
    property int itemLimit: ranked ? 10 : 20
    property var visibleItems: {
        var out = [];
        var count = Math.min(items.length, itemLimit);
        for (var i = 0; i < count; i++)
            out.push(items[i]);
        return out;
    }
    signal itemRequested(var item)

    width: parent ? parent.width : 900
    spacing: 14
    visible: visibleItems.length > 0

    Theme { id: theme }

    WidgetHeader {
        width: parent.width
        title: rail.title
        sub: ""
        moreLabel: "View all"
        navigable: false
    }

    ListView {
        id: list
        width: parent.width
        height: rail.ranked ? 212 : 214
        orientation: ListView.Horizontal
        spacing: rail.ranked ? 30 : 18
        clip: true
        reuseItems: true
        cacheBuffer: width * 0.75
        boundsBehavior: Flickable.StopAtBounds
        model: rail.visibleItems
        leftMargin: 6
        rightMargin: 26

        delegate: Item {
            id: cell
            required property var modelData
            required property int index

            width: rail.ranked ? 184 : 132
            height: list.height

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
