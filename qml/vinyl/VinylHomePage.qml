import QtQuick
Item {
    id: root
    objectName: "vinylHomePage"
    required property var worldData
    required property Item backdrop
    readonly property var rowKeys: worldData.homeRows.map(function(r){return r.key})
    readonly property var coldShelfKeys: worldData.coldShelves.map(function(r){return r.key})
    implicitHeight: picker.visible ? picker.implicitHeight : shelves.implicitHeight
    VinylTastePicker {
        id: picker
        width: parent.width
        visible: !root.worldData.pickerAnswered
        worldData: root.worldData
        backdrop: root.backdrop
    }
    Column {
        id: shelves
        width: parent.width
        spacing: 54
        visible: root.worldData.pickerAnswered
        Repeater {
            model: root.worldData.homeRows.length ? root.worldData.homeRows : root.worldData.coldShelves
            delegate: VinylShelf {
                required property var modelData
                objectName: (root.worldData.homeRows.length ? "vinylHomeRow_" : "vinylHomeColdShelf_")+modelData.key
                worldData: root.worldData
                row: modelData
                home: true
                cold: root.worldData.homeRows.length===0
                width: shelves.width
                cardSize: Math.floor((width-5*24)/6)
            }
        }
    }
}
