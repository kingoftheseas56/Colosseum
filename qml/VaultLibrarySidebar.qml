import QtQuick
import QtQuick.Controls

FocusScope {
    id: root
    objectName: "vaultLibrarySidebar"
    required property Item backdrop
    property var counts: ({})
    property var servers: []
    property string currentView: "home"
    property string currentServer: ""
    property int unmatchedCount: 0
    property string status: ""
    property bool busy: false
    signal selected(string view, string serverId)
    signal sourcesRequested()
    signal rescanRequested()
    Theme { id: theme }
    Glass { anchors.fill: parent; backdrop: root.backdrop; radius: theme.vaultRadius; blurMax: 26; scrim: 0.16 }
    Column {
        id: footer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 12; spacing: 8
        Rectangle { width: parent.width; height: 1; color: theme.edge }
        Row {
            width: parent.width; spacing: 4
            Repeater {
                model: [{ key: "sources", title: "Sources", icon: "vault-folder" }, { key: "hidden", title: "Hidden", icon: "settings" }, { key: "rescan", title: "Rescan", icon: "update" }]
                delegate: Button {
                    id: tile
                    required property var modelData
                    objectName: "vault" + modelData.key + "Tile"
                    width: (footer.width - 8) / 3; height: 58
                    hoverEnabled: true; activeFocusOnTab: true
                    enabled: modelData.key !== "rescan" || !root.busy
                    Accessible.name: modelData.title
                    onClicked: {
                        if (modelData.key === "sources") root.sourcesRequested()
                        else if (modelData.key === "rescan") root.rescanRequested()
                        else root.selected("hidden", "")
                    }
                    background: Rectangle { radius: theme.vaultControlRadius; color: tile.down || tile.hovered ? theme.glassHi : "transparent"; border.width: tile.visualFocus ? 1 : 0; border.color: theme.gold }
                    contentItem: Column {
                        spacing: 5
                        Image { anchors.horizontalCenter: parent.horizontalCenter; width: 18; height: 18; source: "../assets/icons/" + tile.modelData.icon + ".svg"; opacity: tile.enabled ? 0.85 : 0.4 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: tile.modelData.title; color: tile.hovered ? theme.ink : theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall }
                    }
                }
            }
        }
        Row {
            width: parent.width; spacing: 6
            Rectangle { width: 7; height: 7; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: root.busy || (root.status.indexOf("offline") >= 0 || root.status.indexOf("unavailable") >= 0) ? theme.gold : theme.vaultHealthy }
            Text { objectName: "vaultLibraryStatus"; text: root.status; width: parent.width - 13; elide: Text.ElideRight; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall }
        }
    }
    Flickable {
        id: navScroll
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: footer.top
        anchors.margins: 12; anchors.bottomMargin: 10
        clip: true; contentHeight: nav.implicitHeight; boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 4 }
        Column {
            id: nav
            width: navScroll.width - (navScroll.contentHeight > navScroll.height ? 8 : 0); spacing: 2
            Text { text: "LIBRARY"; height: 28; leftPadding: 8; verticalAlignment: Text.AlignVCenter; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall; font.letterSpacing: 1.3 }
            Repeater {
                id: kinds
                model: [{ key: "home", title: "Home", icon: "vault-door" }, { key: "movies", title: "Movies", icon: "movies" }, { key: "shows", title: "Shows", icon: "feria-tv" }, { key: "books", title: "Books", icon: "books" }, { key: "comics", title: "Comics", icon: "comics" }]
                delegate: VaultLibraryNavItem {
                    required property var modelData
                    required property int index
                    objectName: "vaultNav_" + modelData.key
                    width: nav.width; text: modelData.title; iconPath: "../assets/icons/" + modelData.icon + ".svg"
                    active: root.currentView === modelData.key
                    detail: modelData.key === "home" ? "" : String(root.counts[modelData.key] || 0)
                    onClicked: root.selected(modelData.key, "")
                    Keys.onDownPressed: { var next = kinds.itemAt(index + 1); if (next) next.forceActiveFocus(); else matchLink.forceActiveFocus() }
                    Keys.onUpPressed: { var previous = kinds.itemAt(index - 1); if (previous) previous.forceActiveFocus() }
                }
            }
            Text { text: "SERVERS"; height: 30; leftPadding: 8; verticalAlignment: Text.AlignVCenter; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall; font.letterSpacing: 1.3 }
            Repeater {
                model: root.servers
                delegate: VaultLibraryNavItem {
                    required property var modelData
                    width: nav.width; text: modelData.name || modelData.title || "Server"
                    iconPath: "../assets/icons/servers/" + String(modelData.provider || "jellyfin").toLowerCase() + ".svg"
                    active: root.currentView === "server" && root.currentServer === modelData.id
                    detail: modelData.offline ? "offline" : String(modelData.count || 0)
                    onClicked: root.selected("server", modelData.id)
                }
            }
            Text { visible: root.servers.length === 0; height: visible ? 26 : 0; leftPadding: 8; text: "Connect from Sources"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption }
            Rectangle { width: parent.width - 8; x: 4; height: 1; color: theme.edge }
            VaultLibraryNavItem {
                id: matchLink
                objectName: "vaultNav_match"; width: nav.width; text: "Needs a match"; iconPath: "../assets/icons/vault-folder.svg"
                active: root.currentView === "match"; badge: root.unmatchedCount
                onClicked: root.selected("match", "")
            }
        }
    }
}
