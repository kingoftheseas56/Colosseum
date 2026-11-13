pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Flickable {
    id: view
    property var server: ({})
    property var rows: []
    // Preserve server-owned library names. A flat engine response is honestly labelled Library.
    property var groups: []
    readonly property bool dimmed: server.dimmed === true || server.offline === true || server.authRequired === true || server.enabled === false
    readonly property string statusText: {
        switch (String(server.state || "")) {
        case "cached": return "Saved library · Refresh in Sync Center"
        case "pending":
        case "refreshing": return "Updating library…"
        case "unavailable": return "Server unavailable · Reconnect in the Sync Center ›"
        case "needsReconnect": return "Sign-in required · Reconnect in the Sync Center ›"
        case "disabled": return "Server disabled · Manage in the Sync Center ›"
        case "ready": return ""
        }
        if (server.enabled === false) return "Server disabled · Manage in the Sync Center ›"
        if (server.authRequired === true) return "Sign-in required · Reconnect in the Sync Center ›"
        if (server.offline === true) return "Server unavailable · Reconnect in the Sync Center ›"
        return String(server.stateLabel || "")
    }
    signal activated(var row)
    signal reconnectRequested()
    readonly property var libraries: groups.length ? groups : (rows.length ? [{title: "Library", items: rows}] : [])
    Theme { id: theme }
    function focusLibrary(index) {
        const target = libraryRepeater.itemAt(index) as VaultLibraryShelf
        if (target) {
            target.focusFirst()
            contentY = Math.max(0, Math.min(target.y - 20, contentHeight - height))
        }
    }
    contentWidth: width; contentHeight: body.height + 96; clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: HouseScrollBar { flick: view }
    Column {
        id: body; width: view.width; spacing: 24
        Column {
            width: parent.width; spacing: 6
            Text { width: parent.width; text: String(view.server.name || "Your server"); color: theme.ink; font.family: theme.display; font.pixelSize: 40; elide: Text.ElideRight }
            Text { text: String(view.server.type || view.server.provider || "Home server") + " · " + view.rows.length + (view.rows.length === 1 ? " title" : " titles"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
        }
        Rectangle {
            objectName: "vaultServerUnavailableBanner"
            visible: view.statusText.length > 0
            width: parent.width; height: visible ? 58 : 0; radius: 12; color: theme.vaultSurface; border.color: theme.edge
            Text { objectName: "vaultServerStatusText"; anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 16 } text: view.statusText; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
            KeyboardAction { anchors.fill: parent; accessibleName: view.statusText; onTriggered: view.reconnectRequested() }
        }
        Repeater {
            id: libraryRepeater
            model: view.libraries
            delegate: VaultLibraryShelf {
                required property var modelData
                required property int index
                objectName: "vaultServerLibrary_" + index
                width: body.width; title: modelData.title || modelData.name || "Library"; rows: modelData.items || modelData.rows || []
                showSeeAll: false; preserveEmptyHeading: true; hideAllowed: false; dimmed: view.dimmed || modelData.dimmed === true
                onActivated: row => view.activated(row)
                onMoveUp: view.focusLibrary(index - 1)
                onMoveDown: view.focusLibrary(index + 1)
            }
        }
        Text { visible: view.libraries.length === 0; text: "No library titles available. Refresh this server in the Sync Center."; width: parent.width; wrapMode: Text.WordWrap; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14 }
    }
}
