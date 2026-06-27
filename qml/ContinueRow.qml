// ContinueRow — the resume row for a world page. A row IS the right shape for resume
// (per doctrine). Solid portrait covers with a gold progress bar. One of the TWO content
// rows a world page is allowed (the other being Trending).

import QtQuick

Column {
    id: cont

    property string title: "Continue"
    property var items: []              // [{ caption, c1, c2, progress }]
    signal itemClicked(int index)

    width: parent ? parent.width : 800
    spacing: 14

    WidgetHeader { width: parent.width; title: cont.title }

    Flickable {
        width: parent.width; height: 196
        contentWidth: row.width; contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            spacing: 18
            Repeater {
                model: cont.items
                delegate: PortraitTile {
                    required property var modelData
                    required property int index
                    caption: modelData.caption
                    cover: modelData.cover !== undefined ? modelData.cover : ""
                    c1: modelData.c1 !== undefined ? modelData.c1 : "#444"
                    c2: modelData.c2 !== undefined ? modelData.c2 : "#111"
                    progress: modelData.progress !== undefined ? modelData.progress : -1
                    onClicked: cont.itemClicked(index)
                }
            }
        }
    }
}
