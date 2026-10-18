import QtQuick
import ".."
Item {
    id: root
    objectName: "vinylHomePage"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    readonly property var rowKeys: worldData.homeRows.map(function(r){return r.key})
    readonly property var coldShelfKeys: worldData.coldShelves.map(function(r){return r.key})
    implicitHeight: picker.visible ? picker.implicitHeight : shelves.implicitHeight
    Theme {id:theme}
    Text {
        visible: root.worldData.pickerAnswered && !root.worldData.homeRows.some(function(row){return row.items.length>0}) && root.worldData.coldShelves.length===0
        width:parent.width;text:"Your local music will appear here. Open audio files to get started.";color:theme.inkDim;font.family:theme.ui;font.pixelSize:16;wrapMode:Text.WordWrap
    }
    VinylTastePicker {
        id: picker
        width: parent.width
        visible: !root.worldData.pickerAnswered
        worldData: root.worldData
        backdrop: root.backdrop
        pageFlick: root.pageFlick
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
                cardSize: Math.max(154,Math.floor((width-5*24)/6))
            }
        }
    }
}
