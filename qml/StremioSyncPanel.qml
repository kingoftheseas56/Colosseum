pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root
    property bool shown: false
    property var syncState: null
    property var actions: null
    property string confirmation: ""
    property var nuvioAccount: typeof NuvioAccounts !== "undefined" ? NuvioAccounts : null
    property bool showingNuvio: false
    property bool firstRun: false
    property bool firstRunStarted: false
    signal completed()
    signal closeRequested()
    implicitHeight: firstRun ? stremioContent.implicitHeight : 0
    visible: shown
    enabled: visible

    readonly property string status: syncState ? String(syncState.status || "notConnected") : "notConnected"
    readonly property bool busy: (!firstRun && (status === "connecting" || status === "syncing"))
        || stremioImportPhase === "loading" || stremioImportPhase === "applying"
    readonly property bool connected: syncState && syncState.linkedAccount
    readonly property bool nuvioPrimary: nuvioAccount && nuvioAccount.primaryProvider === "nuvio"
    readonly property bool stremioPrimary: nuvioAccount && nuvioAccount.primaryProvider === "stremio"
    readonly property bool stremioImportAvailable: actions && (firstRun
        ? typeof actions.beginStremioFirstRun === "function"
        : typeof actions.previewStremioImport === "function")
    readonly property string stremioImportPhase: stremioImportAvailable
        ? String(actions.stremioImportPhase || "idle") : "idle"
    readonly property bool stremioImportOpen: stremioImportPhase === "preview" || stremioImportPhase === "applying"
    readonly property bool stremioImportBecomesPrimary: stremioImportAvailable
        && actions.stremioImportBecomesPrimary === true
    readonly property string stremioImportMessage: stremioImportAvailable
        ? String(actions.stremioImportMessage || "") : ""

    function formatLastSync(value) {
        const ms = Number(value || 0)
        return ms > 1 ? Qt.formatDateTime(new Date(ms), "MMM d, h:mm AP") : qsTr("Never")
    }
    function heading() {
        if (firstRun && stremioImportPhase === "loading") return qsTr("Connecting to Stremio")
        if (stremioImportPhase === "loading") return qsTr("Preparing Stremio import")
        if (firstRun && stremioImportPhase === "applying") return qsTr("Bringing in your library…")
        if (stremioImportPhase === "applying") return qsTr("Importing from Stremio")
        if (stremioImportPhase === "preview") return qsTr("Review Stremio import")
        if (firstRun) return qsTr("Connect Stremio")
        if (status === "connecting") return qsTr("Connecting to Stremio")
        if (status === "syncing") return qsTr("Syncing with Stremio")
        if (status === "reconnectRequired") return qsTr("Reconnect Stremio")
        if (status === "syncFailed" || status === "paused" || status === "unavailable") return qsTr("Stremio sync needs attention")
        if (status === "synced") return qsTr("Stremio Sync")
        return qsTr("Connect Stremio")
    }
    function detail() {
        if (firstRun) {
            if (stremioImportPhase === "loading")
                return qsTr("Finish signing in through the browser. Colosseum will prepare an import preview.")
            if (stremioImportOpen)
                return qsTr("Review your Stremio library, progress, watched items and addons.")
            return qsTr("Sign in through the browser to preview what you can bring into Colosseum.")
        }
        if (status === "connecting") return qsTr("Finish signing in through the browser window.")
        if (status === "syncing") return qsTr("Combining your library, progress and addons safely.")
        if (status === "reconnectRequired") return qsTr("This profile uses Stremio, but this device needs its own secure connection.")
        if (status === "syncFailed" || status === "paused") return qsTr("Nothing was deleted. Try again when the connection is available.")
        if (status === "unavailable") return qsTr("Stremio Sync is unavailable while this profile is sealed.")
        if (nuvioPrimary && connected) return qsTr("Nuvio is your primary sync service. You can preview an import from Stremio.")
        if (status === "synced") return qsTr("Your newest activity wins. First merge never deletes either library.")
        return qsTr("Bring your Stremio library, progress, History and addon collection into this profile.")
    }
    function primaryLabel() {
        if (firstRun) return qsTr("Sign in to Stremio")
        if (status === "reconnectRequired") return qsTr("Reconnect Stremio")
        if (status === "notConnected") return qsTr("Connect Stremio")
        return nuvioPrimary ? qsTr("Preview import") : qsTr("Sync now")
    }
    function primaryAction() {
        if (busy) return
        if (firstRun && stremioImportAvailable) {
            firstRunStarted = true
            actions.beginStremioFirstRun()
            return
        }
        if (!syncState) return
        if (status === "notConnected" || status === "reconnectRequired") syncState.connectAccount()
        else if (nuvioPrimary) {
            if (stremioImportAvailable) actions.previewStremioImport()
        } else if (actions) actions.syncStremioNow()
    }
    function closePanel() {
        if (firstRun) {
            if (stremioImportPhase === "applying")
                return
            cancelStremioImport()
        }
        confirmation = ""
        closeRequested()
    }

    function cancelStremioImport() {
        if ((!firstRun || stremioImportPhase !== "applying")
                && (stremioImportOpen || stremioImportPhase === "loading" || (firstRun && firstRunStarted))
                && typeof actions.cancelStremioImport === "function") {
            firstRunStarted = false
            actions.cancelStremioImport()
        }
    }
    function selectService(nuvio) {
        if (showingNuvio === nuvio)
            return
        if (showingNuvio && nuvioAccount) {
            nuvioFlow.clearSecrets()
            nuvioAccount.cancel()
        } else {
            cancelStremioImport()
        }
        confirmation = ""
        showingNuvio = nuvio
    }

    function takeKeyboardFocus() {
        if (shown && primaryAction.visible && primaryAction.enabled)
            primaryAction.focusInput()
    }

    onShownChanged: {
        if (shown) {
            if (!firstRun)
                Qt.callLater(root.takeKeyboardFocus)
        } else {
            selectService(false)
            cancelStremioImport()
            confirmation = ""
        }
    }
    onStremioImportPhaseChanged: {
        if (firstRun && firstRunStarted && stremioImportPhase === "done") {
            firstRunStarted = false
            completed()
        }
    }
    Keys.onEscapePressed: {
        if (firstRun) closePanel()
        else if (confirmation.length > 0) confirmation = ""
        else if (!showingNuvio && stremioImportOpen) cancelStremioImport()
        else closePanel()
    }

    Rectangle { anchors.fill: parent; visible: !root.firstRun; color: Qt.rgba(0.01, 0.015, 0.025, 0.82) }
    MouseArea { anchors.fill: parent; enabled: !root.firstRun }
    Theme { id: theme }

    Rectangle {
        objectName: "stremioSyncPanel"
        anchors.centerIn: parent
        width: root.firstRun ? parent.width : Math.min(620, parent.width - 48)
        height: root.firstRun ? parent.height : Math.min(parent.height - 48, root.nuvioAccount ? 560 : 470)
        radius: 24
        color: root.firstRun ? "transparent" : "#151821"
        border.width: root.firstRun ? 0 : 1
        border.color: theme.edge

        Flow {
            id: serviceSwitch
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 24
            visible: !root.firstRun && !!root.nuvioAccount
            spacing: 8

            PanelAction {
                objectName: "syncStremioService"
                label: qsTr("Stremio")
                primary: !root.showingNuvio
                onTriggered: root.selectService(false)
            }
            PanelAction {
                objectName: "syncNuvioService"
                label: qsTr("Nuvio")
                primary: root.showingNuvio
                onTriggered: root.selectService(true)
            }
            PanelAction {
                objectName: "nuvioCloseSyncPanel"
                label: qsTr("Close")
                visible: root.showingNuvio
                onTriggered: root.closePanel()
            }
        }

        NuvioAccountFlow {
            id: nuvioFlow
            anchors.fill: parent
            anchors.margins: 24
            anchors.topMargin: 36 + serviceSwitch.implicitHeight
            visible: root.showingNuvio
            account: root.nuvioAccount
        }

        ScrollView {
            id: stremioScroll
            anchors.fill: parent
            anchors.margins: root.firstRun ? 0 : 24
            anchors.topMargin: root.firstRun ? 0 : serviceSwitch.visible ? 36 + serviceSwitch.implicitHeight : 34
            visible: !root.showingNuvio
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: root.firstRun ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded

            Column {
                id: stremioContent
                width: stremioScroll.availableWidth
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
                        Text { width: parent.width; wrapMode: Text.Wrap; text: root.heading(); color: theme.ink; font.family: theme.display; font.pixelSize: 30 }
                        Text { width: parent.width; text: root.detail(); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Grid {
                    visible: !root.firstRun
                    columns: 2
                    columnSpacing: 28
                    rowSpacing: 8
                    Text { text: qsTr("Account"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                    Text { text: root.syncState && root.syncState.accountDisplayName ? root.syncState.accountDisplayName : qsTr("Not connected"); color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                    Text { text: qsTr("Last sync"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                    Text { text: root.formatLastSync(root.syncState ? root.syncState.lastSuccessAt : 0); color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                }
                Text {
                    width: parent.width
                    visible: !root.firstRun && root.connected && root.stremioPrimary
                    text: qsTr("Primary sync service")
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                Text {
                    objectName: "stremioSyncResult"
                    width: parent.width
                    visible: !root.firstRun && root.syncState && String(root.syncState.lastResultSummary || "").length > 0
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
                Text {
                    width: parent.width
                    visible: root.stremioImportMessage.length > 0
                    text: root.stremioImportMessage
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
                NuvioAccountFlow.ImportPreview {
                    width: parent.width
                    visible: root.stremioImportOpen
                    rows: visible ? root.actions.stremioImportPreview : []
                }
                Text {
                    width: parent.width
                    visible: !root.firstRun && root.stremioImportOpen
                        && (root.stremioImportBecomesPrimary || root.nuvioPrimary)
                    text: root.stremioImportBecomesPrimary
                        ? qsTr("Stremio will become your primary sync service, and playback progress will be imported.")
                        : qsTr("Imported library and watched items will also appear in Nuvio while it is your primary sync service. Playback progress is included only when Stremio becomes primary.")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
                Flow {
                    width: parent.width
                    spacing: 12
                    PanelAction {
                        id: primaryAction
                        objectName: "stremioSyncPrimaryAction"
                        label: root.confirmation.length > 0
                            ? (root.confirmation === "switch" ? qsTr("Confirm switch") : qsTr("Confirm disconnect"))
                            : root.primaryLabel()
                        primary: true
                        visible: !root.stremioImportOpen
                        enabled: !root.busy && (root.firstRun ? root.stremioImportAvailable
                            : (!root.nuvioPrimary || root.stremioImportAvailable
                                || root.status === "notConnected" || root.status === "reconnectRequired"
                                || root.confirmation.length > 0)
                            && root.status !== "unavailable"
                            && root.status !== "paused")
                        onTriggered: {
                            if (root.confirmation === "switch") { root.confirmation = ""; root.syncState.switchAccount() }
                            else if (root.confirmation === "disconnect") { root.confirmation = ""; root.syncState.disconnectCurrentProfile() }
                            else root.primaryAction()
                        }
                    }
                    PanelAction {
                        objectName: "stremioSwitchAccountAction"
                        label: qsTr("Switch account")
                        visible: !root.firstRun && root.connected && root.confirmation.length === 0 && !root.stremioImportOpen
                        onTriggered: root.confirmation = "switch"
                    }
                    PanelAction {
                        objectName: "stremioDisconnectAction"
                        label: qsTr("Disconnect")
                        visible: !root.firstRun && root.connected && root.confirmation.length === 0 && !root.stremioImportOpen
                        onTriggered: root.confirmation = "disconnect"
                    }
                    PanelAction {
                        objectName: "stremioApplyImport"
                        label: root.firstRun ? qsTr("Import and continue")
                            : root.stremioImportBecomesPrimary ? qsTr("Import and use Stremio as primary")
                            : qsTr("Import into Colosseum")
                        primary: true
                        visible: root.stremioImportOpen
                        enabled: !root.busy && typeof root.actions.applyStremioImport === "function"
                        onTriggered: {
                            if (root.firstRun)
                                root.actions.applyStremioImport(true)
                            else
                                root.actions.applyStremioImport()
                        }
                    }
                    PanelAction {
                        objectName: "stremioCancelImport"
                        label: qsTr("Cancel import")
                        visible: !root.firstRun && root.stremioImportOpen
                        onTriggered: root.cancelStremioImport()
                    }
                    PanelAction {
                        objectName: "stremioMakePrimary"
                        label: qsTr("Use Stremio as primary")
                        visible: !root.firstRun && root.nuvioPrimary && root.connected && !root.stremioImportOpen
                            && root.confirmation.length === 0 && root.actions
                            && typeof root.actions.makeStremioPrimary === "function"
                        enabled: !root.busy && root.status !== "reconnectRequired"
                            && root.status !== "unavailable" && root.status !== "paused"
                        onTriggered: root.actions.makeStremioPrimary()
                    }
                    PanelAction {
                        objectName: "stremioCancelAction"
                        label: root.firstRun || root.confirmation.length > 0 ? qsTr("Cancel") : qsTr("Close")
                        enabled: !root.firstRun || root.stremioImportPhase !== "applying"
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
        width: labelText.implicitWidth + 30
        height: 42
        radius: 10
        color: primary ? theme.gold : Qt.rgba(1, 1, 1, 0.06)
        border.width: primary ? 0 : 1
        border.color: theme.edge
        opacity: enabled ? 1 : 0.5
        Text { id: labelText; anchors.centerIn: parent; text: action.label; color: action.primary ? "#17130a" : theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
        KeyboardAction { id: actionInput; anchors.fill: parent; accessibleName: action.label; focusRadius: action.radius; onTriggered: action.triggered() }
    }
}
