import QtQuick
import ".."
Item {
    id: root
    objectName: "vinylListPage"
    required property var worldData
    required property Item backdrop
    property string listKey: ""
    property bool home: false
    property bool showBack: true
    property Flickable pageFlick: null
    property int currentIndex: 0
    property string selectedId: ""
    function focusCurrent() {
        var item=cards.itemAt(Math.max(0,Math.min(currentIndex,cards.count-1)));
        if(item) item.forceActiveFocus();
        else listBack.focusControl(Qt.TabFocusReason);
    }
    function reveal(index) {
        var item=cards.itemAt(index);
        if(!item || !pageFlick) return;
        var top=item.mapToItem(pageFlick.contentItem,0,0).y;
        if(top<pageFlick.contentY) pageFlick.contentY=top;
        else if(top+item.height>pageFlick.contentY+pageFlick.height) pageFlick.contentY=top+item.height-pageFlick.height;
        Qt.callLater(focusCurrent);
    }
    Keys.onPressed: event => nav.handle(event)
    KeyboardCollectionController {
        id: nav; view: root; count: root.itemCount; orientation: "grid"; columns: grid.columns
        positionIndexFn: function(index){root.reveal(index)}
        onActivated: index => root.worldData.activate(root.row.items[index])
    }
    onRowChanged: {
        var index=row.items.findIndex(function(item){return item.id===root.selectedId});
        currentIndex=index>=0 ? index : Math.max(0,Math.min(currentIndex,row.items.length-1));
    }
    readonly property var row: worldData.row(listKey,home)
    readonly property string title: row.title
    readonly property int itemCount: row.items.length
    readonly property bool onlineList: !home && worldData.onlineMode === true
    readonly property bool searchPagingSupported: worldData.cataloguePagingSupported !== false
    readonly property bool rowLoading: row.loading === true
    readonly property string rowError: String(row.error || "")
    readonly property bool showLoadMore: onlineList && worldData.discoveryLanding !== true
        && ((searchPagingSupported && row.hasMore === true) || rowLoading || rowError.length > 0)
    signal backRequested()
    implicitHeight: Math.max(header.y+header.height+26+grid.implicitHeight,
        emptyMessage.visible ? emptyMessage.y+emptyMessage.implicitHeight : 0,
        loadMoreRow.visible ? loadMoreRow.y+loadMoreRow.height : 0)
    Theme { id: theme }
    Item {
        id: header
        y: 0
        width: parent.width
        height: 50
        BackAction {
            focusOnPointer: false
            id: listBack
            objectName: "vinylListBack"
            visible: root.showBack
            label: "Back"
            height: 44
            onTriggered: root.backRequested()
        }
        Text {
            x: root.showBack ? 120 : 0
            width: parent.width-x
            elide: Text.ElideRight
            text: root.title
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 34
        }
    }
    Text {
        id: emptyMessage
        y: grid.y + grid.implicitHeight + 18
        width: parent.width
        visible: root.itemCount === 0
            && !(root.onlineList && (root.rowLoading || root.rowError.length > 0))
        text: root.onlineList ? "No catalogue matches."
            : "No local music in this collection. Open audio files or change your filters."
        textFormat: Text.PlainText
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16; wrapMode: Text.WordWrap
    }
    Grid {
        id: grid
        y: header.y+header.height+26
        width: parent.width
        columns: Math.max(1,Math.min(6,Math.floor((width+24)/180)))
        columnSpacing: 24
        rowSpacing: 32
        Repeater {
            id: cards
            model: root.row.items
            delegate: VinylItemCard {
                required property var modelData
                required property int index
                objectName: "vinylListCard_"+index
                worldData: root.worldData
                item: modelData
                onActiveFocusChanged: if(activeFocus) {root.currentIndex=index;root.selectedId=modelData.id}
                Keys.onPressed: event => { if([Qt.Key_Left,Qt.Key_Right,Qt.Key_Up,Qt.Key_Down,Qt.Key_Home,Qt.Key_End,Qt.Key_PageUp,Qt.Key_PageDown].indexOf(event.key)>=0) nav.handle(event); }
                width: (grid.width-(grid.columns-1)*24)/grid.columns
                height: Math.max(implicitHeight,width+110)
            }
        }
    }
    Item {
        id: loadMoreRow
        objectName: "vinylListMoreRow"
        y: grid.y + grid.implicitHeight + (emptyMessage.visible ? emptyMessage.implicitHeight + 12 : 0) + 18
        width: parent.width
        height: visible ? Math.max(38, loadMoreStatus.implicitHeight) : 0
        visible: root.showLoadMore
        Text {
            id: loadMoreStatus
            x: 0
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - (loadMoreButton.visible ? 50 : 0))
            height: implicitHeight
            text: root.rowLoading ? "Loading more…" : root.rowError
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        VinylWorldButton {
            id: loadMoreButton
            objectName: "vinylListMore"
            x: parent.width - width
            y: (parent.height - height) / 2
            width: 38
            height: 38
            backdrop: root.backdrop
            label: "Load more results"
            symbol: "arrowDown"
            enabled: !root.rowLoading
            onTriggered: {
                if (typeof root.worldData.loadMoreSearch === "function")
                    root.worldData.loadMoreSearch();
            }
        }
    }
}
