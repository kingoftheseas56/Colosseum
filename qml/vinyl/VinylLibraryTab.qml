import QtQuick
import ".."

// Read model only. The caller supplies rows and owns activation, feedback and history.
Item {
    id: root
    objectName: "vinylLibrary"
    property var rows: []
    property var worldData: null
    property Item backdrop: null
    property Flickable pageFlick: null
    property real cardSize: 130
    readonly property var presentRows: {
        var source = root.rows && typeof root.rows.length === "number" ? root.rows : [];
        var order = ["playlists", "saved-albums", "liked-songs", "followed-artists", "downloads", "recently-played"];
        var result = [];
        for (var key of order) {
            var row = null;
            for (var i = 0; i < source.length; ++i) {
                if (source[i] && source[i].key === key) { row = source[i]; break; }
            }
            if (row && row.items && typeof row.items.length === "number" && row.items.length > 0)
                result.push(row);
        }
        return result;
    }
    readonly property string rowKeys: presentRows.map(function(row) { return row.key; }).join(",")
    implicitHeight: content.implicitHeight
    signal itemActivated(var item)
    signal listRequested(var row)

    Theme { id: theme }
    QtObject {
        id: cards
        property var session: root.worldData && root.worldData.session ? root.worldData.session : null
        property string persistenceError: root.worldData && root.worldData.persistenceError ? String(root.worldData.persistenceError) : ""
        function artwork(value) {
            return root.worldData && typeof root.worldData.artwork === "function"
                ? root.worldData.artwork(value) : String(value || "");
        }
        function activate(item) { root.itemActivated(item); }
        property var pinCrateItem: root.worldData && typeof root.worldData.pinCrateItem==="function"
            ? function(item,pinned){return root.worldData.pinCrateItem(item,pinned);} : undefined
        property var isCratePinned: root.worldData && typeof root.worldData.isCratePinned==="function"
            ? function(item){return root.worldData.isCratePinned(item);} : undefined
        function openList(key, home) {
            for (var i = 0; i < root.presentRows.length; ++i)
                if (root.presentRows[i].key === key) { root.listRequested(root.presentRows[i]); return; }
        }
        property var setRecommendationFeedback: root.worldData && typeof root.worldData.setRecommendationFeedback === "function"
            ? function(ref, action, generation) { return root.worldData.setRecommendationFeedback(ref, action, generation); } : undefined
        property var recommendationProfileGeneration: root.worldData && typeof root.worldData.recommendationProfileGeneration === "function"
            ? function() { return root.worldData.recommendationProfileGeneration(); } : undefined
        property var recommendationArtistRef: root.worldData && typeof root.worldData.recommendationArtistRef === "function"
            ? function(item) { return root.worldData.recommendationArtistRef(item); } : undefined
        property var requestRecommendationActions: root.worldData && typeof root.worldData.requestRecommendationActions === "function"
            ? function(item, row) { return root.worldData.requestRecommendationActions(item, row); } : undefined
        property var recordVisibleRecommendation: root.worldData && typeof root.worldData.recordVisibleRecommendation === "function"
            ? function(row, item, generation, visible) { return root.worldData.recordVisibleRecommendation(row, item, generation, visible); } : undefined
    }
    Column {
        id: content
        width: parent.width
        spacing: 30
        Item {
            width: parent.width
            height: Math.max(38, emptyLibraryHint.implicitHeight)
            Text {
                id: emptyLibraryHint
                width: Math.max(0, parent.width - 54)
                anchors.verticalCenter: parent.verticalCenter
                visible: root.presentRows.length === 0
                text: "Albums from your library will appear here."
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            VinylWorldButton {
                objectName: "vinylLibraryImportFolder"
                anchors.right: parent.right
                width: 38
                height: 38
                label: "Add music folder"
                symbol: "musicFolder"
                backdrop: root.backdrop
                onTriggered: {
                    if (root.worldData && root.worldData.personalLibrary)
                        root.worldData.personalLibrary.importFolderRequested();
                }
            }
        }
        Column {
            width: parent.width
            spacing: 8
            visible: jobs.count>0
            Text { visible: jobs.count>0;text:qsTr("Downloading");color:theme.ink;font.family:theme.display;font.pixelSize:24 }
            Repeater {
                id:jobs
                model: root.worldData && root.worldData.downloadJobs ? root.worldData.downloadJobs : []
                delegate: Row {
                    required property var modelData
                    objectName:"vinylDownloadJob_"+modelData.id
                    width:content.width;spacing:8
                    Text {
                        width:Math.max(0,parent.width-290);height:34;verticalAlignment:Text.AlignVCenter
                        text:modelData.title+" · "+(modelData.state==="downloading" ? Math.round(modelData.ratio*100)+"%" : modelData.error || modelData.state)
                        color:theme.inkDim;font.family:theme.ui;font.pixelSize:13;elide:Text.ElideRight
                    }
                    VinylWorldButton {
                        objectName:"vinylDownloadControl_"+modelData.id
                        width:130;height:34
                        label:modelData.state==="failed" ? qsTr("Retry") : modelData.state==="paused" ? qsTr("Resume") : qsTr("Pause")
                        onTriggered:root.worldData.controlMusicDownload(modelData.id,modelData.state==="failed" ? "retry" : modelData.state==="paused" ? "resume" : "pause")
                    }
                    VinylWorldButton { objectName:"vinylDownloadCancel_"+modelData.id;width:130;height:34;label:qsTr("Cancel");onTriggered:root.worldData.controlMusicDownload(modelData.id,"cancel") }
                }
            }
        }
        Repeater {
            model: root.presentRows
            delegate: VinylShelf {
                required property var modelData
                readonly property int itemCount: modelData.items.length
                objectName: "vinylLibraryRow_" + modelData.key
                width: content.width
                row: modelData
                worldData: cards
                backdrop: root.backdrop
                pageFlick: root.pageFlick
                cardSize: root.cardSize
                home: modelData.recommendation === true
            }
        }
    }
}
