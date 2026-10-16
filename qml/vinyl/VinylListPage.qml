import QtQuick
import ".."
Item {
    id: root
    objectName: "vinylListPage"
    required property var worldData
    required property Item backdrop
    property string listKey: ""
    property bool home: false
    readonly property var row: worldData.row(listKey,home)
    readonly property string title: row.title
    readonly property int itemCount: row.items.length
    signal backRequested()
    implicitHeight: header.y+header.height+26+grid.implicitHeight
    Theme { id: theme }
    Item {
        id: header
        y: 0
        width: parent.width
        height: 50
        VinylWorldButton {
            objectName: "vinylListBack"
            label: "‹ Back"
            backdrop: root.backdrop
            onTriggered: root.backRequested()
        }
        Text {
            x: 120
            text: root.title
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 34
        }
    }
    Grid {
        id: grid
        y: header.y+header.height+26
        width: parent.width
        columns: width>1300 ? 6 : 5
        columnSpacing: 24
        rowSpacing: 32
        Repeater {
            model: root.row.items
            delegate: VinylItemCard {
                required property var modelData
                required property int index
                objectName: "vinylListCard_"+index
                worldData: root.worldData
                item: modelData
                width: (grid.width-(grid.columns-1)*24)/grid.columns
                height: Math.max(implicitHeight,width+110)
            }
        }
    }
}
