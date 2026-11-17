import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    property bool expanded: true
    readonly property int collapsedWidth: 62
    readonly property bool compactFooter: height < 620
    readonly property int navRowHeight: root.compactFooter ? (root.expanded ? 32 : 28) : theme.vaultRowHeight
    readonly property int providerRowHeight: root.compactFooter ? 30 : theme.vaultRowHeight
    readonly property int footerRowHeight: root.compactFooter ? 30 : theme.vaultRowHeight
    readonly property int utilityRowHeight: root.compactFooter ? 28 : (root.expanded ? 58 : 28)
    signal selected(string view, string serverId)
    signal sourcesRequested()
    signal rescanRequested()
    signal addFolderRequested()
    signal toggleRequested()
    width: expanded ? theme.vaultSidebarWidth : collapsedWidth
    Behavior on width { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
    Theme { id: theme }
    function focusNav(index) {
        if (index < 0) toggle.forceActiveFocus(Qt.TabFocusReason)
        else if (index < kinds.count) kinds.itemAt(index).forceActiveFocus(Qt.TabFocusReason)
        else if (index === kinds.count) matchLink.forceActiveFocus(Qt.TabFocusReason)
        else if (index < kinds.count + 1 + serverItems.count) serverItems.itemAt(index - kinds.count - 1).forceActiveFocus(Qt.TabFocusReason)
        else addFolder.forceActiveFocus(Qt.TabFocusReason)
    }
    function revealNavItem(item) {
        let ancestor = item
        while (ancestor && ancestor !== nav) ancestor = ancestor.parent
        if (!ancestor) return
        const top = item.mapToItem(nav, 0, 0).y
        const bottom = top + item.height
        if (top < navArea.contentY) navArea.contentY = top
        else if (bottom > navArea.contentY + navArea.height) navArea.contentY = bottom - navArea.height
    }
    function revealFocusedNavItem() {
        navArea.contentY = Math.max(0, Math.min(navArea.contentY, navArea.contentHeight - navArea.height))
        if (root.Window.window) revealNavItem(root.Window.window.activeFocusItem)
    }
    Glass { anchors.fill: parent; backdrop: root.backdrop; radius: theme.vaultRadius; blurMax: 26; scrim: 0.16 }
    Item {
        id: header
        objectName: "vaultSidebarHeader"
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: root.compactFooter ? 2 : root.expanded ? 12 : 9
        height: 38
        Text { visible: root.expanded; anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: qsTr("LIBRARY"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall; font.letterSpacing: 1.3 }
        Button {
            id: toggle
            objectName: "vaultSidebarToggle"
            anchors.right: parent.right
            width: root.expanded ? 38 : parent.width; height: 38
            hoverEnabled: true; activeFocusOnTab: true
            Accessible.name: root.expanded ? qsTr("Collapse library sidebar") : qsTr("Expand library sidebar")
            ToolTip.text: Accessible.name; ToolTip.delay: 350; ToolTip.visible: hovered || activeFocus
            onClicked: root.toggleRequested()
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            Keys.onDownPressed: root.focusNav(0)
            background: Rectangle { radius: theme.vaultControlRadius; color: toggle.down || toggle.hovered ? theme.glassHi : "transparent"; border.width: toggle.visualFocus ? 1 : 0; border.color: theme.gold }
            contentItem: Item {
                Image { anchors.centerIn: parent; width: 18; height: 18; sourceSize.width: 36; source: "../assets/icons/lucide/chevron-left.svg"; rotation: root.expanded ? 0 : 180 }
            }
        }
    }
    Flickable {
        id: navArea
        objectName: "vaultSidebarNavigation"
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: header.bottom; anchors.bottom: footer.top
        anchors.leftMargin: root.compactFooter ? 2 : root.expanded ? 12 : 9
        anchors.rightMargin: root.compactFooter ? 2 : root.expanded ? 12 : 9
        anchors.topMargin: root.compactFooter ? 1 : 6
        anchors.bottomMargin: root.compactFooter ? 1 : 10
        clip: true
        contentWidth: width
        contentHeight: nav.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: contentHeight > height
        onHeightChanged: Qt.callLater(root.revealFocusedNavItem)
        onContentHeightChanged: Qt.callLater(root.revealFocusedNavItem)
        ScrollBar.vertical: ScrollBar {
            objectName: "vaultSidebarScrollBar"
            policy: navArea.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            width: 4
        }
        Column {
            id: nav
            objectName: "vaultSidebarNavigationItems"
            width: navArea.width - (navArea.interactive ? 8 : 0); spacing: root.compactFooter ? 1 : 2
            Repeater {
                id: kinds
                model: [{ key: "home", title: qsTr("Home"), icon: "vault-door" }, { key: "movies", title: qsTr("Movies"), icon: "movies" }, { key: "shows", title: qsTr("Shows"), icon: "feria-tv" }, { key: "books", title: qsTr("Books"), icon: "books" }, { key: "comics", title: qsTr("Comics"), icon: "comics" }]
                delegate: VaultLibraryNavItem {
                    required property var modelData
                    required property int index
                    objectName: "vaultNav_" + modelData.key
                    width: nav.width; text: modelData.title; iconPath: "../assets/icons/" + modelData.icon + ".svg"
                    height: root.navRowHeight
                    collapsed: !root.expanded
                    active: root.currentView === modelData.key
                    detail: modelData.key === "home" ? "" : String(root.counts[modelData.key] || 0)
                    onActiveFocusChanged: if (activeFocus) root.revealNavItem(this)
                    onClicked: root.selected(modelData.key, "")
                    Keys.onDownPressed: root.focusNav(index + 1)
                    Keys.onUpPressed: root.focusNav(index - 1)
                }
            }
            Rectangle { width: parent.width - 8; x: 4; height: 1; color: theme.edge }
            VaultLibraryNavItem {
                id: matchLink
                objectName: "vaultNav_match"; width: nav.width; height: root.navRowHeight
                text: qsTr("Unmatched"); iconPath: "../assets/icons/lucide/circle-alert.svg"
                collapsed: !root.expanded
                active: root.currentView === "match"; badge: root.unmatchedCount
                onActiveFocusChanged: if (activeFocus) root.revealNavItem(this)
                onClicked: root.selected("match", "")
                Keys.onDownPressed: root.focusNav(kinds.count + 1)
                Keys.onUpPressed: root.focusNav(kinds.count - 1)
            }
            Column {
                id: providerGroup
                objectName: "vaultSidebarProviderGroup"
                width: parent.width
                height: implicitHeight
                spacing: root.compactFooter ? 0 : 2
                Text {
                    visible: root.expanded && !root.compactFooter
                    width: parent.width; height: visible ? 22 : 0
                    text: qsTr("SERVERS")
                    leftPadding: 8; verticalAlignment: Text.AlignVCenter
                    color: theme.inkDimmer; font.family: theme.ui
                    font.pixelSize: theme.vaultSmall; font.letterSpacing: 1.3
                }
                Repeater {
                    id: serverItems
                    model: ["jellyfin", "emby", "plex"]
                    delegate: VaultLibraryNavItem {
                        required property string modelData
                        required property int index
                        readonly property var providerRow: root.servers.find(row => row.id === modelData)
                            || ({connected:false, enabled:false,
                                 name:modelData === "jellyfin" ? "Jellyfin" : modelData === "emby" ? "Emby" : "Plex"})
                        readonly property string providerStateText: {
                            if (providerRow.connected === false) return qsTr("Not connected")
                            if (providerRow.enabled === false) return qsTr("Connections disabled")
                            if (providerRow.authRequired) return qsTr("Sign-in required")
                            if (providerRow.offline) return qsTr("Server unavailable")
                            if (providerRow.state === "refreshing") return qsTr("Refreshing libraries")
                            if (providerRow.statusText) return providerRow.statusText
                            return Number(providerRow.count || 0) > 0
                                ? qsTr("Connected · %1 items").arg(Number(providerRow.count))
                                : qsTr("Connected")
                        }
                        objectName: "vaultNav_provider_" + modelData
                        width: providerGroup.width; text: providerRow.name
                        height: root.providerRowHeight
                        iconPath: "../assets/icons/servers/" + modelData + ".svg"
                        iconSize: 22
                        statusKind: providerRow.connected === false || providerRow.enabled === false ? "unlinked"
                            : providerRow.offline || providerRow.authRequired || providerRow.state === "unavailable"
                              || providerRow.state === "needsReconnect"
                              || String(providerRow.statusText || "").toLowerCase().indexOf("unavailable") >= 0
                              || String(providerRow.statusText || "").toLowerCase().indexOf("sign-in") >= 0 ? "attention"
                            : providerRow.state === "refreshing" ? "busy" : "linked"
                        statusText: providerStateText
                        collapsed: !root.expanded
                        active: root.currentView === "server" && root.currentServer === modelData
                        detail: String(providerRow.count || 0)
                        onActiveFocusChanged: if (activeFocus) root.revealNavItem(this)
                        onClicked: root.selected("server", modelData)
                        Keys.onDownPressed: root.focusNav(kinds.count + index + 2)
                        Keys.onUpPressed: root.focusNav(kinds.count + index)
                    }
                }
            }
        }
    }
    Column {
        id: footer
        objectName: "vaultSidebarFooter"
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: implicitHeight
        anchors.margins: root.compactFooter ? 2 : root.expanded ? 12 : 9
        spacing: root.compactFooter ? 1 : 8
        VaultLibraryNavItem {
            id: addFolder
            objectName: "vaultSidebarAddFolder"
            width: parent.width; height: root.footerRowHeight
            text: qsTr("Add folder"); iconPath: "../assets/icons/vault-folder.svg"; addIcon: true
            collapsed: !root.expanded
            onClicked: root.addFolderRequested()
            Keys.onUpPressed: root.focusNav(kinds.count + serverItems.count)
            Keys.onDownPressed: utilityItems.itemAt(0).forceActiveFocus(Qt.TabFocusReason)
        }
        Rectangle { width: parent.width; height: 1; color: theme.edge }
        GridLayout {
            id: utilityGrid
            objectName: "vaultSidebarUtilities"
            width: parent.width
            height: root.compactFooter ? (root.expanded ? root.utilityRowHeight : 3 * root.utilityRowHeight + 4)
                    : root.expanded ? 58 : 3 * 28 + 8
            columns: root.expanded ? 3 : 1
            rowSpacing: root.expanded ? 0 : root.compactFooter ? 2 : 4
            columnSpacing: 4
            Repeater {
                id: utilityItems
                model: [{ key: "sources", title: qsTr("Sources"), icon: "vault-folder" }, { key: "hidden", title: qsTr("Hidden"), icon: "settings" }, { key: "rescan", title: qsTr("Rescan"), icon: "update" }]
                delegate: Button {
                    id: tile
                    required property var modelData
                    required property int index
                    objectName: "vault" + modelData.key + "Tile"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.utilityRowHeight
                    Layout.minimumHeight: Layout.preferredHeight
                    hoverEnabled: true; activeFocusOnTab: true
                    enabled: modelData.key !== "rescan" || !root.busy
                    Accessible.name: modelData.key === "rescan" && root.status.length > 0
                                     ? modelData.title + ", " + root.status : modelData.title
                    ToolTip.text: modelData.key === "rescan" && root.status.length > 0
                                  ? root.status : modelData.title
                    ToolTip.delay: 350
                    ToolTip.visible: (root.compactFooter || !root.expanded) && (hovered || activeFocus)
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    Keys.onLeftPressed: { if (root.expanded && index > 0) utilityItems.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason) }
                    Keys.onRightPressed: { if (root.expanded && index < 2) utilityItems.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason) }
                    Keys.onUpPressed: { if (!root.expanded && index > 0) utilityItems.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason); else addFolder.forceActiveFocus(Qt.TabFocusReason) }
                    Keys.onDownPressed: { if (!root.expanded && index < 2) utilityItems.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason) }
                    onClicked: {
                        if (modelData.key === "sources") root.sourcesRequested()
                        else if (modelData.key === "rescan") root.rescanRequested()
                        else root.selected("hidden", "")
                    }
                    background: Rectangle { radius: theme.vaultControlRadius; color: tile.down || tile.hovered || (tile.modelData.key === "hidden" && root.currentView === "hidden") ? theme.glassHi : "transparent"; border.width: tile.visualFocus ? 1 : 0; border.color: theme.gold }
                    contentItem: Item {
                        Image { anchors.horizontalCenter: parent.horizontalCenter; y: root.expanded && !root.compactFooter ? 2 : (parent.height - height) / 2; width: 18; height: 18; sourceSize.width: 36; source: "../assets/icons/" + tile.modelData.icon + ".svg"; opacity: tile.enabled ? 0.85 : 0.4 }
                        Text { visible: root.expanded && !root.compactFooter; anchors.horizontalCenter: parent.horizontalCenter; y: 25; text: tile.modelData.title; color: tile.hovered ? theme.ink : theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall }
                        Rectangle {
                            objectName: "vaultRescanStatusDot"
                            visible: tile.modelData.key === "rescan" && root.compactFooter && root.status.length > 0
                            x: parent.width - width - 2; y: parent.height - height - 2
                            width: 6; height: 6; radius: 3
                            color: root.status === qsTr("No sources connected") ? theme.inkDimmer
                                   : root.busy || root.status.toLowerCase().indexOf("offline") >= 0
                                     || root.status.toLowerCase().indexOf("unavailable") >= 0 ? theme.gold : theme.vaultHealthy
                            border.width: 1; border.color: theme.vaultSurface
                        }
                    }
                }
            }
        }
        Item {
            objectName: "vaultLibrarySourcesStatus"
            visible: root.expanded && !root.compactFooter && root.status.length > 0
            width: parent.width; height: 15
            activeFocusOnTab: true
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Sources status: %1").arg(root.status)
            ToolTip.text: root.status; ToolTip.delay: 350; ToolTip.visible: statusHover.hovered || activeFocus
            HoverHandler { id: statusHover }
            Rectangle { x: 0; width: 7; height: 7; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: root.status === qsTr("No sources connected") ? theme.inkDimmer : root.busy || (root.status.indexOf("offline") >= 0 || root.status.indexOf("unavailable") >= 0) ? theme.gold : theme.vaultHealthy }
            Text { objectName: "vaultLibraryStatus"; visible: root.expanded && !root.compactFooter && root.status !== qsTr("No sources connected"); text: root.status; x: 13; width: parent.width - 13; elide: Text.ElideRight; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall }
        }
    }
}
