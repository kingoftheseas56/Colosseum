pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: root
    property bool shown: false
    property var syncState: null
    property var actions: null
    property string confirmation: ""
    property bool touchInput: Qt.platform.os === "android"
    signal closeRequested()
    visible: shown
    enabled: visible

    readonly property string status: syncState ? String(syncState.status || "notConnected") : "notConnected"
    readonly property bool busy: status === "connecting" || status === "syncing"
    readonly property bool connected: syncState && syncState.linkedAccount

    function formatLastSync(value) {
        const ms = Number(value || 0)
        return ms > 1 ? Qt.formatDateTime(new Date(ms), "MMM d, h:mm AP") : qsTr("Never")
    }
    function heading() {
        if (status === "connecting") return qsTr("Connecting to Stremio")
        if (status === "syncing") return qsTr("Syncing with Stremio")
        if (status === "reconnectRequired") return qsTr("Reconnect Stremio")
        if (status === "syncFailed" || status === "paused" || status === "unavailable") return qsTr("Stremio sync needs attention")
        if (status === "synced") return qsTr("Stremio Sync")
        return qsTr("Connect Stremio")
    }
    function detail() {
        if (status === "connecting") return qsTr("Finish signing in through the browser window.")
        if (status === "syncing") return qsTr("Combining your library, progress and addons safely.")
        if (status === "reconnectRequired") return qsTr("This profile uses Stremio, but this device needs its own secure connection.")
        if (status === "syncFailed" || status === "paused") return qsTr("Nothing was deleted. Try again when the connection is available.")
        if (status === "unavailable") return qsTr("Stremio Sync is unavailable while this profile is sealed.")
        if (status === "synced") return qsTr("Your newest activity wins. First merge never deletes either library.")
        return qsTr("Bring your Stremio library, progress, History and addon collection into this profile.")
    }
    function primaryLabel() {
        if (status === "reconnectRequired") return qsTr("Reconnect Stremio")
        if (status === "notConnected") return qsTr("Connect Stremio")
        return qsTr("Sync now")
    }
    function primaryAction() {
        if (busy || !syncState) return
        if (status === "notConnected" || status === "reconnectRequired") syncState.connectAccount()
        else if (actions) actions.syncStremioNow()
    }
    function closePanel() {
        confirmation = ""
        closeRequested()
    }
    function revealPanelAction(item) {
        if (!root.touchInput || !root.shown || !item) return
        let ancestor = item
        while (ancestor && ancestor !== panelScroll.contentItem) ancestor = ancestor.parent
        if (!ancestor) return
        const point = item.mapToItem(panelScroll.contentItem, 0, 0)
        let targetY = panelScroll.contentY
        if (point.y < targetY) targetY = point.y
        else if (point.y + item.height > targetY + panelScroll.height)
            targetY = point.y + item.height - panelScroll.height
        panelScroll.contentY = Math.max(0, Math.min(targetY,
            Math.max(0, panelScroll.contentHeight - panelScroll.height)))
        ancestor = item
        while (ancestor && ancestor !== actionScroll.contentItem) ancestor = ancestor.parent
        if (!ancestor) return
        const actionPoint = item.mapToItem(actionScroll.contentItem, 0, 0)
        let targetX = actionScroll.contentX
        if (actionPoint.x < targetX) targetX = actionPoint.x
        else if (actionPoint.x + item.width > targetX + actionScroll.width)
            targetX = actionPoint.x + item.width - actionScroll.width
        actionScroll.contentX = Math.max(0, Math.min(targetX,
            Math.max(0, actionScroll.contentWidth - actionScroll.width)))
    }
    function revealFocusedControl() {
        const window = root.Window.window
        root.revealPanelAction(window ? window.activeFocusItem : null)
    }

    onShownChanged: {
        if (panelScroll) panelScroll.cancelFlick()
        if (actionScroll) actionScroll.cancelFlick()
        if (shown) Qt.callLater(function() {
            panelScroll.contentY = 0
            actionScroll.contentX = 0
            primaryAction.focusInput()
        })
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { Qt.callLater(root.revealFocusedControl) }
    }
    Keys.onEscapePressed: {
        if (confirmation.length > 0) confirmation = ""
        else closePanel()
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0.01, 0.015, 0.025, 0.82) }
    MouseArea { anchors.fill: parent }
    Theme { id: theme }

    Rectangle {
        objectName: "stremioSyncPanel"
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        height: root.touchInput
            ? Math.max(0, Math.min(root.confirmation.length > 0 ? 470 : 440, parent.height - 24))
            : root.confirmation.length > 0 ? 470 : 440
        radius: 24
        color: "#151821"
        border.width: 1
        border.color: theme.edge

        Flickable {
            id: panelScroll
            objectName: "stremioSyncScroll"
            anchors.fill: parent
            anchors.margins: 34
            contentWidth: width
            contentHeight: panelContent.implicitHeight
            clip: root.touchInput
            interactive: root.touchInput && contentHeight > height
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            onHeightChanged: Qt.callLater(root.revealFocusedControl)
            ScrollBar.vertical: ScrollBar { policy: root.touchInput ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            Column {
                id: panelContent
                width: panelScroll.width
                spacing: 18
                Row {
                    width: parent.width
                    spacing: 14
                    Image {
                        objectName: "stremioPanelOfficialAsset"
                        width: 38; height: 38
                        source: "../assets/icons/stremio-official.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                    Column {
                        width: parent.width - 52
                        spacing: 5
                        Text { text: root.heading(); color: theme.ink; font.family: theme.display; font.pixelSize: 30 }
                        Text { width: parent.width; text: root.detail(); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Grid {
                    columns: 2
                    columnSpacing: 28
                    rowSpacing: 8
                    Text { text: qsTr("Account"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                    Text { text: root.syncState && root.syncState.accountDisplayName ? root.syncState.accountDisplayName : qsTr("Not connected"); color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                    Text { text: qsTr("Last sync"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                    Text { text: root.formatLastSync(root.syncState ? root.syncState.lastSuccessAt : 0); color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                }
                Text {
                    objectName: "stremioSyncResult"
                    width: parent.width
                    visible: root.syncState && String(root.syncState.lastResultSummary || "").length > 0
                    text: visible ? root.syncState.lastResultSummary : ""
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: root.confirmation.length > 0
                    width: parent.width
                    text: root.confirmation === "switch"
                        ? qsTr("Switching accounts keeps everything already merged, then connects a replacement Stremio account.")
                        : qsTr("Disconnecting stops sync and removes this device's credential. Everything already merged stays in Colosseum.")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Flickable {
                    id: actionScroll
                    objectName: "stremioSyncActionScroll"
                    // Preserve the desktop Row; overflow gets horizontal touch panning.
                    width: parent.width
                    height: panelActions.implicitHeight
                    contentWidth: Math.max(width, panelActions.implicitWidth)
                    contentHeight: height
                    clip: root.touchInput
                    interactive: root.touchInput && contentWidth > width
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    onWidthChanged: Qt.callLater(root.revealFocusedControl)
                    // The focused action can move when confirmation changes Row membership.
                    onContentWidthChanged: Qt.callLater(root.revealFocusedControl)
                    Row {
                        id: panelActions
                        spacing: 12
                        PanelAction {
                            id: primaryAction
                            objectName: "stremioSyncPrimaryAction"
                            label: root.confirmation.length > 0
                                ? (root.confirmation === "switch" ? qsTr("Confirm switch") : qsTr("Confirm disconnect"))
                                : root.primaryLabel()
                            primary: true
                            enabled: !root.busy
                                && root.status !== "unavailable"
                                && root.status !== "paused"
                            onTriggered: {
                                if (root.confirmation === "switch") { root.confirmation = ""; root.syncState.switchAccount() }
                                else if (root.confirmation === "disconnect") { root.confirmation = ""; root.syncState.disconnectCurrentProfile() }
                                else root.primaryAction()
                            }
                        }
                        PanelAction {
                            objectName: "stremioSwitchAccountAction"
                            label: qsTr("Switch account")
                            visible: root.connected && root.confirmation.length === 0
                            onTriggered: root.confirmation = "switch"
                        }
                        PanelAction {
                            objectName: "stremioDisconnectAction"
                            label: qsTr("Disconnect")
                            visible: root.connected && root.confirmation.length === 0
                            onTriggered: root.confirmation = "disconnect"
                        }
                        PanelAction {
                            objectName: "stremioCancelAction"
                            label: root.confirmation.length > 0 ? qsTr("Cancel") : qsTr("Close")
                            onTriggered: {
                                if (root.confirmation.length > 0) root.confirmation = ""
                                else root.closePanel()
                            }
                        }
                    }
                }
            }
        }
    }

    component PanelAction: Rectangle {
        id: action
        property string label: ""
        property bool primary: false
        signal triggered()
        function focusInput() { actionInput.forceActiveFocus(Qt.PopupFocusReason) }
        width: labelText.implicitWidth + 30
        height: 42
        radius: 10
        color: primary ? theme.gold : Qt.rgba(1, 1, 1, 0.06)
        border.width: primary ? 0 : 1
        border.color: theme.edge
        opacity: enabled ? 1 : 0.5
        Text { id: labelText; anchors.centerIn: parent; text: action.label; color: action.primary ? "#17130a" : theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
        KeyboardAction {
            id: actionInput; anchors.fill: parent; accessibleName: action.label; focusRadius: action.radius
            onActiveFocusChanged: if (activeFocus) root.revealPanelAction(action)
            onTriggered: action.triggered()
        }
    }
}
