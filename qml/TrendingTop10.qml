// TrendingTop10 — a ranked "Top 10" widget. A non-row shape that VARIES the board (big ghost
// rank numerals behind solid covers) instead of stacking another plain row. This counts as the
// second and last content row a world page is allowed.

import QtQuick

Column {
    id: top

    property string title: "Top 10 This Week"
    property var items: []            // [{ caption, c1, c2 }]
    signal itemClicked(int index)

    width: parent ? parent.width : 800
    spacing: 14

    Theme { id: theme }

    WidgetHeader { width: parent.width; title: top.title }

    Flickable {
        width: parent.width; height: 212
        contentWidth: row.width; contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            spacing: 30; leftPadding: 6
            Repeater {
                model: top.items
                delegate: Item {
                    id: rank
                    required property var modelData
                    required property int index
                    width: num.width + cov.width - 32
                    height: 212

                    Text {
                        id: num
                        text: (rank.index + 1)
                        color: Qt.rgba(1, 1, 1, 0.16)
                        font.family: theme.display; font.bold: true; font.pixelSize: 132
                        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.bottomMargin: -8
                    }
                    PortraitTile {
                        id: cov
                        width: 132; height: 196
                        anchors.left: num.right; anchors.leftMargin: -32; anchors.bottom: parent.bottom
                        caption: rank.modelData.caption
                        cover: rank.modelData.cover !== undefined ? rank.modelData.cover : ""
                        c1: rank.modelData.c1 !== undefined ? rank.modelData.c1 : "#444"
                        c2: rank.modelData.c2 !== undefined ? rank.modelData.c2 : "#111"
                        onClicked: top.itemClicked(rank.index)
                    }
                }
            }
        }
    }
}
