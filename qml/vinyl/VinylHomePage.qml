import QtQuick
import ".."

Item {
    id: root
    objectName: "vinylHomePage"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick

    readonly property var homeRows: worldData.homeRows || []
    readonly property var eligibleHomeRows: homeRows.filter(function(row) {
        return row && !root.isExcludedHomeRow(row);
    })
    readonly property var rowKeys: visibleHomeRows.map(function(row) { return row.key; })
    readonly property var coldShelfKeys: (worldData.coldShelves || []).map(function(row) { return row.key; })
    readonly property bool showTastePicker: worldData.tastePickerOpen === true
    readonly property bool showTasteEdit: worldData.pickerAnswered === true && !showTastePicker
        && typeof worldData.beginTasteEdit === "function"
    readonly property bool showHomeRefresh: !showTastePicker && typeof worldData.refreshHomeSelection === "function"
    readonly property bool hasHomeItems: eligibleHomeRows.some(function(row) {
        return row && row.items && row.items.length > 0;
    })
    readonly property bool hasRowLoadingState: visibleHomeRows.some(function(row) {
        return row && row.status === "loading";
    })
    readonly property bool hasRowFailureState: visibleHomeRows.some(function(row) {
        return row && (row.status === "stale" || row.status === "error");
    })
    readonly property bool showRecommendationStatus: !showTastePicker && hasHomeItems
        && ((worldData.homeStatus === "loading" && !hasRowLoadingState)
            || ((worldData.homeStatus === "partial" || worldData.homeStatus === "error"
                || String(worldData.homeError || "").length > 0) && !hasRowFailureState))
    readonly property bool showRecommendationRetry: showRecommendationStatus
        && worldData.homeStatus !== "loading"
        && typeof worldData.retryHomeRecommendations === "function"
    readonly property var visibleHomeRows: eligibleHomeRows.filter(function(row) {
        if(!row) return false;
        if(row.items && row.items.length > 0) return true;
        return hasHomeItems && (row.recommendation === true || row.personal === false)
            && ["loading", "stale", "error"].indexOf(String(row.status || "")) >= 0;
    })
    readonly property bool hasHomeRows: visibleHomeRows.length > 0
    readonly property bool showEmptyRetry: !showTastePicker && !hasHomeItems
        && (worldData.homeStatus === "partial" || worldData.homeStatus === "error"
            || String(worldData.homeError || "").length > 0
            || eligibleHomeRows.some(function(row) {
                return row && (row.status === "stale" || row.status === "error");
            }))
        && (typeof worldData.retryHomeRow === "function"
            || typeof worldData.retryHomeRecommendations === "function")
    readonly property int cardColumns: Math.max(1, Math.min(6, Math.ceil(width / 204)))
    readonly property real cardSize: Math.max(120,
        Math.floor((width - (cardColumns - 1) * 24) / cardColumns))
    readonly property real contentStartY: showTasteEdit || showHomeRefresh ? 56 : 0
    readonly property real recommendationStatusExtent: showRecommendationStatus ? 50 : 0
    readonly property string emptyHomeMessage: {
        var status=String(worldData.homeStatus || "");
        if(status === "loading" || eligibleHomeRows.some(function(row) { return row && row.status === "loading"; }))
            return "Loading music…";
        if(status === "error" || status === "partial" || status === "stale"
            || eligibleHomeRows.some(function(row) { return row && (row.status === "stale" || row.status === "error"); })
            || String(worldData.homeError || "").length > 0) return "Catalogue unavailable.";
        return "No music yet.";
    }

    function isExcludedHomeRow(row) {
        var identity=(String(row.key || "")+" "+String(row.title || ""))
            .toLowerCase().replace(/[_-]+/g," ").replace(/\s+/g," ").trim();
        return /(^| )(regional|trending|charts?|popular)( |$)/.test(identity)
            || /(^| )(saved|owned) playlists?( |$)/.test(identity)
            || /(^| )(playlist|collection) management( |$)/.test(identity);
    }
    function focusArtistSearch() {
        if(!root.showTastePicker)return false;
        picker.focusArtistSearch();return true;
    }

    function retryEmptyHome() {
        var row=eligibleHomeRows.find(function(candidate) {
            return candidate && candidate.canRetry === true
                && (candidate.status === "error" || candidate.status === "stale");
        });
        if(row && typeof worldData.retryHomeRow === "function")
            return worldData.retryHomeRow(row);
        if(typeof worldData.retryHomeRecommendations === "function")
            return worldData.retryHomeRecommendations();
        return false;
    }

    implicitHeight: showTastePicker
        ? picker.implicitHeight
        : hasHomeItems
            ? shelves.y + shelves.implicitHeight
            : hasHomeRows
                ? shelves.y + shelves.implicitHeight + 24
                : emptyState.y + emptyState.implicitHeight + 24

    Theme { id: theme }

    Row {
        id: homeActions
        objectName: "vinylHomeActions"
        x: Math.max(0, parent.width - width)
        y: 0
        spacing: root.showHomeRefresh && root.showTasteEdit ? 8 : 0
        visible: root.showTasteEdit || root.showHomeRefresh

        VinylWorldButton {
            objectName: "vinylHomeRefresh"
            width: visible ? 38 : 0
            height: 38
            visible: root.showHomeRefresh
            enabled: visible
            label: qsTr("Refresh Home")
            symbol: "retry"
            backdrop: root.backdrop
            onTriggered: root.worldData.refreshHomeSelection()
        }

        VinylWorldButton {
            objectName: "vinylTasteEdit"
            width: visible ? 38 : 0
            height: 38
            visible: root.showTasteEdit
            enabled: visible
            label: qsTr("Edit tastes")
            symbol: "edit"
            backdrop: root.backdrop
            onTriggered: {
                if (typeof root.worldData.beginTasteEdit === "function")
                    root.worldData.beginTasteEdit()
            }
        }
    }

    Column {
        id: emptyState
        objectName: "vinylHomeEmpty"
        x: Math.max(0, (parent.width - width) / 2)
        y: root.contentStartY + (root.showTasteEdit ? 8 : 24) + root.recommendationStatusExtent
        width: Math.min(parent.width, 480)
        spacing: 18
        visible: !root.showTastePicker && !root.hasHomeItems

        Text {
            objectName: "vinylHomeEmptyMessage"
            width: parent.width
            text: root.emptyHomeMessage
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            color: theme.inkDim
            font.family: theme.display
            font.pixelSize: 24
            wrapMode: Text.WordWrap
        }

        Item {
            width: parent.width
            height: 38

            Row {
                anchors.centerIn: parent
                spacing: 12

                VinylWorldButton {
                    objectName: "vinylHomeDiscover"
                    width: 38
                    height: 38
                    backdrop: root.backdrop
                    label: "Discover"
                    symbol: "discover"
                    onTriggered: root.worldData.openHomeDestination("discover")
                }

                VinylWorldButton {
                    objectName: "vinylHomeLibrary"
                    width: 38
                    height: 38
                    backdrop: root.backdrop
                    label: "Library"
                    symbol: "musicFolder"
                    onTriggered: root.worldData.openHomeDestination("library")
                }

                VinylWorldButton {
                    objectName: "vinylHomeRetry"
                    width: 38
                    height: 38
                    visible: root.showEmptyRetry
                    enabled: visible
                    backdrop: root.backdrop
                    label: "Retry"
                    symbol: "retry"
                    onTriggered: root.retryEmptyHome()
                }
            }
        }
    }

    VinylTastePicker {
        id: picker
        width: parent.width
        visible: root.showTastePicker
        worldData: root.worldData
        backdrop: root.backdrop
        pageFlick: root.pageFlick
    }

    Item {
        id: recommendationStatus
        objectName: "vinylHomeRecommendationsStatus"
        x: 0
        y: root.contentStartY
        width: parent.width
        height: root.showRecommendationStatus ? 38 : 0
        visible: root.showRecommendationStatus

        Text {
            objectName: "vinylHomeRecommendationsStatusText"
            x: 0
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - (retryRecommendations.visible ? 54 : 0))
            text: root.worldData.homeStatus === "loading"
                ? "Updating recommendations…"
                : "Some recommendations could not be loaded."
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        VinylWorldButton {
            id: retryRecommendations
            objectName: "vinylHomeRecommendationsRetry"
            x: parent.width - width
            y: 0
            width: 38
            height: 38
            visible: root.showRecommendationRetry
            enabled: visible
            label: "Retry recommendations"
            symbol: "retry"
            backdrop: root.backdrop
            onTriggered: root.worldData.retryHomeRecommendations()
        }
    }

    Binding {
        target: shelves
        property: "y"
        value: root.contentStartY + root.recommendationStatusExtent
            + (!root.hasHomeItems && root.hasHomeRows
                ? emptyState.implicitHeight + (root.showTasteEdit ? 8 : 24) : 0)
    }

    Column {
        id: shelves
        width: parent.width
        spacing: 54
        visible: !root.showTastePicker && root.hasHomeRows

        Repeater {
            model: root.visibleHomeRows

            delegate: VinylShelf {
                required property var modelData
                objectName: "vinylHomeRow_" + modelData.key
                worldData: root.worldData
                row: modelData
                home: true
                cold: false
                backdrop: root.backdrop
                pageFlick: root.pageFlick
                homePageVisible: root.visible && root.worldData.activeTab === "home"
                    && !root.showTastePicker
                width: shelves.width
                cardSize: root.cardSize
            }
        }
    }
}
