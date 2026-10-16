import QtQuick
import ".."
Item {
    id: root
    objectName: "vinylDiscoverPage"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    readonly property var populatedShelves: worldData.shelves.filter(function(row){return row.items && row.items.length>0})
    readonly property int shelfCount: populatedShelves.length
    readonly property real cardSize: Math.floor((width - sidebar.contentLeft - 5*24) / 6)
    implicitHeight: Math.max(80,shelves.implicitHeight)
    Text {
        objectName: "vinylDiscoverEmpty"
        visible: root.shelfCount===0
        x:0;width:parent.width-x;text:"No local music matches. Open files or change the filters.";color:theme.inkDim;font.family:theme.ui;font.pixelSize:16;wrapMode:Text.WordWrap
    }
    Column {
        id: shelves
        x: sidebar.contentLeft
        y: 0
        width: parent.width-x
        spacing: 54
        Repeater {
            id: shelfRepeater
            model: root.populatedShelves
            delegate: VinylShelf {
                required property var modelData
                objectName: "vinylShelf_" + modelData.key
                width: shelves.width
                worldData: root.worldData
                row: modelData
                cardSize: Math.max(154,root.cardSize)
            }
        }
    }
    Theme { id: theme }
    function icon(key) {
        var shapes={
            popular:'<path d="M4 20V12h4v8M10 20V4h4v16M16 20V8h4v12"/>',
            trending:'<path d="m3 17 6-6 4 3 8-10M15 4h6v6"/>',
            new:'<path d="m12 2 2.5 7.5L22 12l-7.5 2.5L12 22l-2.5-7.5L2 12l7.5-2.5z"/>',
            essentials:'<path d="m12 3 3 6 7 1-5 5 1 7-6-3-6 3 1-7-5-5 7-1z"/>',
            global:'<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18"/>',
            threads:'<circle cx="5" cy="6" r="2"/><circle cx="19" cy="6" r="2"/><circle cx="12" cy="19" r="2"/><path d="m6 8 5 9M18 8l-5 9"/>'
        }
        return "data:image/svg+xml,"+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="'+theme.inkDim+'" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">'+(shapes[key]||shapes.threads)+'</svg>')
    }
    RowIndexSidebar {
        id: sidebar
        objectName: "vinylDiscoverRowIndex"
        visible: root.shelfCount>0
        pageFlick: root.pageFlick
        flowHost: shelves
        backdrop: root.backdrop
        worldName: "Vinyl"
        automationPrefix: "vinylWorldRowIndex"
        rows: {
            var result=[]
            shelfRepeater.count
            for(var i=0;i<shelfRepeater.count;i++) {
                var target=shelfRepeater.itemAt(i), data=root.populatedShelves[i]
                if(target && data) result.push({key:data.key,title:data.title,target:target,iconSource:root.icon(data.icon)})
            }
            return result
        }
        onRowRequested: target => {
            root.pageFlick.cancelFlick()
            root.pageFlick.contentY=target.mapToItem(root.pageFlick.contentItem,0,0).y-sidebar.pinLine
        }
    }
}
