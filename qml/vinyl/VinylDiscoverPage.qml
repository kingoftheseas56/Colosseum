import QtQuick
import ".."
Item {
    id: root
    objectName: "vinylDiscoverPage"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    readonly property bool discoveryLanding: worldData.discoveryLanding === true
    readonly property bool discoveryLoading: worldData.discoveryLoading === true
    readonly property string discoveryError: String(worldData.discoveryError || "")
    readonly property bool discoveryCached: worldData.discoveryCached === true
    readonly property string discoveryFetchedAt: String(worldData.discoveryFetchedAt || "")
    readonly property var populatedShelves: (worldData.shelves || []).filter(function(row){return row && row.items && row.items.length>0})
    readonly property int shelfCount: populatedShelves.length
    readonly property real contentInset: sidebar.visible ? sidebar.contentLeft : 0
    readonly property real shelfWidth: Math.max(0, width - contentInset)
    readonly property int cardColumns: Math.max(1, Math.min(6, Math.ceil(shelfWidth / 204)))
    readonly property real cardSize: Math.max(120, Math.floor((shelfWidth - (cardColumns - 1) * 24) / cardColumns))
    readonly property string discoveryStatus: {
        if (!discoveryLanding) return "";
        if (discoveryLoading) return "Loading music…";
        if (discoveryError.length > 0) return discoveryError;
        if (discoveryCached && shelfCount > 0) return "Cached catalogue";
        if (shelfCount === 0) return "No music available.";
        return "";
    }
    readonly property bool showDiscoveryRetry: discoveryLanding && !discoveryLoading
        && (discoveryError.length > 0 || shelfCount === 0 || discoveryCached)
    implicitHeight: Math.max(80,
        emptyMessage.visible ? emptyMessage.implicitHeight : 0,
        statusRow.visible ? statusRow.y + statusRow.height : 0,
        shelves.y + shelves.implicitHeight)
    Text {
        id: emptyMessage
        objectName: "vinylDiscoverEmpty"
        visible: root.shelfCount===0 && !root.discoveryLanding
        x:0;width:parent.width-x
        text:root.worldData.onlineMode ? (root.worldData.query.trim() ? "No catalogue matches. Try another search." : "Search the catalogue to find artists and albums.") : "No local music matches. Change the filters or add music in Library."
        textFormat: Text.PlainText
        color:theme.inkDim;font.family:theme.ui;font.pixelSize:16;wrapMode:Text.WordWrap
    }
    Item {
        id: statusRow
        objectName: "vinylDiscoverStatus"
        x: root.contentInset
        y: 0
        width: root.shelfWidth
        visible: root.discoveryLanding && root.discoveryStatus.length > 0
        height: visible ? Math.max(38, statusLabel.implicitHeight) : 0
        Text {
            id: statusLabel
            x: 0
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - (retryButton.visible ? 50 : 0))
            height: implicitHeight
            text: root.discoveryStatus
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            Accessible.description: root.discoveryCached && root.discoveryFetchedAt.length > 0
                ? "Last fetched " + root.discoveryFetchedAt : ""
        }
        VinylWorldButton {
            id: retryButton
            objectName: "vinylDiscoverRetry"
            visible: root.showDiscoveryRetry
            x: parent.width - width
            y: (parent.height - height) / 2
            width: 38
            height: 38
            backdrop: root.backdrop
            label: root.discoveryError.length || !root.shelfCount ? "Retry" : "Refresh catalogue"
            symbol: "retry"
            onTriggered: {
                if (typeof root.worldData.retryDiscovery === "function")
                    root.worldData.retryDiscovery();
            }
        }
    }
    Column {
        id: shelves
        x: root.contentInset
        y: statusRow.visible ? statusRow.height + 24 : 0
        width: root.shelfWidth
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
                cardSize: root.cardSize
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
        objectName: "vinylWorldRowIndexSidebar"
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
