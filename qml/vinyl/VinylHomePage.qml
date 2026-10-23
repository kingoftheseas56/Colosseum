import QtQuick
import ".."

Item {
    id: root
    objectName: "vinylHomePage"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick

    readonly property var homeRows: worldData.homeRows || []
    readonly property var rowKeys: homeRows.map(function(row) { return row.key; })
    readonly property var coldShelfKeys: (worldData.coldShelves || []).map(function(row) { return row.key; })
    readonly property bool showTastePicker: worldData.tastePickerOpen === true
    readonly property bool showTasteEdit: worldData.pickerAnswered === true && !showTastePicker
        && typeof worldData.beginTasteEdit === "function"
    readonly property bool showRecommendationStatus: !showTastePicker
        && (worldData.homeStatus === "loading" || worldData.homeStatus === "partial"
            || worldData.homeStatus === "error" || String(worldData.homeError || "").length > 0)
    readonly property bool showRecommendationRetry: showRecommendationStatus
        && worldData.homeStatus !== "loading"
        && typeof worldData.retryHomeRecommendations === "function"
    readonly property var visibleHomeRows: homeRows.filter(function(row) {
        return row && row.items && row.items.length > 0;
    })
    readonly property bool hasHomeItems: visibleHomeRows.length > 0
    readonly property int cardColumns: Math.max(1, Math.min(6, Math.ceil(width / 204)))
    readonly property real cardSize: Math.max(120,
        Math.floor((width - (cardColumns - 1) * 24) / cardColumns))
    readonly property real contentStartY: showTasteEdit ? 56 : 0
    readonly property real recommendationStatusExtent: showRecommendationStatus ? 50 : 0

    implicitHeight: showTastePicker
        ? picker.implicitHeight
        : hasHomeItems
            ? shelves.y + shelves.implicitHeight
            : emptyState.y + emptyState.implicitHeight + 24

    Theme { id: theme }

    VinylWorldButton {
        objectName: "vinylTasteEdit"
        x: Math.max(0, parent.width - width)
        y: 0
        visible: root.showTasteEdit
        enabled: visible
        label: "Edit tastes"
        accessibleName: label
        symbol: "edit"
        backdrop: root.backdrop
        onTriggered: {
            if (typeof root.worldData.beginTasteEdit === "function")
                root.worldData.beginTasteEdit()
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
            width: parent.width
            text: "No music yet."
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
                    objectName: "vinylHomeLibrary"
                    width: 38
                    height: 38
                    backdrop: root.backdrop
                    label: "Library"
                    symbol: "musicFolder"
                    onTriggered: root.worldData.openHomeDestination("library")
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
            x: 0
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - (retryRecommendations.visible ? 54 : 0))
            text: root.worldData.homeStatus === "loading"
                ? "Updating recommendations…" : "Some recommendations need a refresh."
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

    Column {
        id: shelves
        y: root.contentStartY + root.recommendationStatusExtent
        width: parent.width
        spacing: 54
        visible: !root.showTastePicker && root.hasHomeItems

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
