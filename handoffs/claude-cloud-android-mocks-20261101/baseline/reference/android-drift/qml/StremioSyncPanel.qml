pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

SettingsScaffold {
    id: root
    property bool shown: false
    property var syncState: null
    property var actions: null
    property string confirmation: ""
    signal closeRequested()
    visible: shown
    enabled: visible
    readonly property bool compactPhoneLayout:layout.phoneLayout
    readonly property var activePhoneSheet:layout.touch ? phoneSheet : null
    PhoneModalSheet {
        id:phoneSheet
        title:root.heading()
        hostedItem:panelContent
        inputMethod:root.inputMethod
        safeInsets:root.safeInsets
        dismissHandler:function() { if(root.confirmation.length>0) { root.confirmation="";return true }return false }
        onClosed:if(root.shown)root.closePanel()
    }

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
        if(layout.touch) { phoneSheet.reveal(item);return }
        const itemY = item.mapToItem(panelScroll.contentItem, 0, 0).y
        if (itemY < panelScroll.contentY)
            panelScroll.contentY = itemY
        else if (itemY + item.height > panelScroll.contentY + panelScroll.height)
            panelScroll.contentY = itemY + item.height - panelScroll.height
    }

    onShownChanged: {
        if(layout.touch) { if(shown)Qt.callLater(function(){if(root.shown)phoneSheet.open()});else phoneSheet.close() }
        if(shown)Qt.callLater(function(){primaryAction.focusInput()})
    }
    Component.onCompleted:if(shown && layout.touch)Qt.callLater(function(){if(root.shown)phoneSheet.open()})
    Keys.onEscapePressed: {
        if (confirmation.length > 0) confirmation = ""
        else closePanel()
    }

    Rectangle { visible:!root.layout.touch; anchors.fill: parent; color: Qt.rgba(0.01, 0.015, 0.025, 0.82) }
    MouseArea { enabled:!root.layout.touch; anchors.fill: parent }
    Theme { id: theme }

    Rectangle {
        objectName: "stremioSyncPanel"
        visible:!root.layout.touch
        anchors.centerIn: parent
        width: root.compactPhoneLayout
            ? Math.max(1, Math.min(620, parent.width - 24))
            : Math.min(620, parent.width - 48)
        height: root.compactPhoneLayout
            ? Math.max(1, Math.min(root.confirmation.length > 0 ? 470 : 440, parent.height - 24))
            : root.confirmation.length > 0 ? 470 : 440
        radius: root.compactPhoneLayout ? 16 : 24
        color: "#151821"
        border.width: 1
        border.color: theme.edge

        Flickable {
            id: panelScroll
            anchors.fill: parent
            anchors.margins: root.compactPhoneLayout ? 12 : 34
            clip: true
            contentWidth: width
            contentHeight: panelContent.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            ScrollBar.vertical: ScrollBar {
                policy: panelScroll.contentHeight > panelScroll.height
                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            Column {
                id: panelContent
                width: panelScroll.width
                spacing: root.compactPhoneLayout ? 10 : 18

                Row {
                    id: panelHeading
                    width: parent.width
                    spacing: root.compactPhoneLayout ? 8 : 14
                    Image {
                        id: panelIcon
                        objectName: "stremioPanelOfficialAsset"
                        width: root.compactPhoneLayout ? 30 : 38
                        height: width
                        source: "../assets/icons/stremio-official.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                    Column {
                        width: parent.width - panelIcon.width
                            - (root.compactPhoneLayout ? panelClose.width : 0)
                            - panelHeading.spacing * (root.compactPhoneLayout ? 2 : 1)
                        spacing: 5
                        Text {
                            width: parent.width
                            text: root.heading()
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: theme.fontSize(root.compactPhoneLayout ? 22 : 30)
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width
                            text: root.detail()
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(13)
                            wrapMode: Text.WordWrap
                        }
                    }
                    PanelAction {
                        id: panelClose
                        objectName: "stremioSyncPanelClose"
                        visible: root.compactPhoneLayout
                        label: root.confirmation.length > 0 ? qsTr("Cancel") : qsTr("Close")
                        onTriggered: {
                            if (root.confirmation.length > 0) root.confirmation = ""
                            else root.closePanel()
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: theme.edge }

                Grid {
                    width: parent.width
                    columns: 2
                    columnSpacing: root.compactPhoneLayout ? 12 : 28
                    rowSpacing: 8
                    readonly property real labelColumnWidth:
                        Math.max(accountLabel.implicitWidth, lastSyncLabel.implicitWidth)
                    Text {
                        id: accountLabel
                        width: parent.labelColumnWidth
                        text: qsTr("Account")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(12)
                    }
                    Text {
                        width: parent.width - parent.labelColumnWidth - parent.columnSpacing
                        text: root.syncState && root.syncState.accountDisplayName
                            ? root.syncState.accountDisplayName : qsTr("Not connected")
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(12)
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        id: lastSyncLabel
                        width: parent.labelColumnWidth
                        text: qsTr("Last sync")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(12)
                    }
                    Text {
                        width: parent.width - parent.labelColumnWidth - parent.columnSpacing
                        text: root.formatLastSync(root.syncState ? root.syncState.lastSuccessAt : 0)
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(12)
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    objectName: "stremioSyncResult"
                    width: parent.width
                    visible: root.syncState && String(root.syncState.lastResultSummary || "").length > 0
                    text: visible ? root.syncState.lastResultSummary : ""
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: theme.fontSize(13)
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
                    font.pixelSize: theme.fontSize(13)
                    wrapMode: Text.WordWrap
                }

                Flow {
                    width: parent.width
                    spacing: root.compactPhoneLayout ? 8 : 12
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

    component PanelAction: Rectangle {
        id: action
        property string label: ""
        property bool primary: false
        signal triggered()
        function focusInput() { actionInput.forceActiveFocus(Qt.PopupFocusReason) }
        width: root.layout.targetSize(labelText.implicitWidth + 30)
        height: root.layout.targetSize(root.compactPhoneLayout ? 48 : 42)
        radius: 10
        color: primary ? theme.gold : Qt.rgba(1, 1, 1, 0.06)
        border.width: primary ? 0 : 1
        border.color: theme.edge
        opacity: enabled ? 1 : 0.5
        Text { id: labelText; anchors.centerIn: parent; text: action.label; color: action.primary ? "#17130a" : theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(13); font.weight: Font.DemiBold }
        KeyboardAction {
            id: actionInput
            anchors.fill: parent
            accessibleName: action.label
            focusRadius: action.radius
            onActiveFocusChanged: if (activeFocus) root.revealPanelAction(action)
            onTriggered: action.triggered()
        }
    }
}
