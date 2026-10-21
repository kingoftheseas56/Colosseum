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
    readonly property var visibleHomeRows: homeRows.filter(function(row) {
        return row && row.items && row.items.length > 0;
    })
    readonly property bool hasHomeItems: visibleHomeRows.length > 0
    readonly property int cardColumns: Math.max(1, Math.min(6, Math.ceil(width / 204)))
    readonly property real cardSize: Math.max(120,
        Math.floor((width - (cardColumns - 1) * 24) / cardColumns))

    implicitHeight: showTastePicker
        ? picker.implicitHeight
        : hasHomeItems
            ? shelves.implicitHeight
            : emptyState.y + emptyState.implicitHeight + 24

    Theme { id: theme }

    Column {
        id: emptyState
        objectName: "vinylHomeEmpty"
        x: Math.max(0, (parent.width - width) / 2)
        y: 24
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

    Column {
        id: shelves
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
                width: shelves.width
                cardSize: root.cardSize
            }
        }
    }
}
