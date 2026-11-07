import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ScrollView {
    id: root

    required property var account
    property bool firstRun: false
    signal completed()
    signal cancelRequested()

    readonly property string phase: account ? account.phase : "idle"
    readonly property bool busy: account && account.busy
    readonly property bool connected: account && account.connected
    readonly property bool reconnect: account && account.needsReconnect
    readonly property string primaryProvider: account ? account.primaryProvider : ""
    readonly property bool previewBecomesPrimary: firstRun
        || (account && account.previewBecomesPrimary === true)
    readonly property bool showSignIn: account && account.available
        && (phase === "signIn" || ((phase === "idle" || phase === "done") && (!connected || reconnect)))
    property bool awaitingCompletion: false

    implicitHeight: flowContent.implicitHeight
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: firstRun ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded

    function takeKeyboardFocus() {
        if (emailInput.visible && emailInput.enabled)
            emailInput.forceActiveFocus(Qt.TabFocusReason)
    }

    function clearSecrets() {
        emailInput.clear()
        passwordInput.clear()
    }

    function cancelFlow() {
        if (firstRun && awaitingCompletion && busy)
            return
        awaitingCompletion = false
        clearSecrets()
        if (account)
            account.cancel()
        cancelRequested()
    }

    function signIn() {
        if (!signInButton.enabled)
            return
        const email = emailInput.text.trim()
        const password = passwordInput.text
        passwordInput.clear()
        account.signIn(email, password)
    }

    function finishFirstRun() {
        if (awaitingCompletion && phase === "done" && !busy) {
            awaitingCompletion = false
            clearSecrets()
            completed()
        }
    }

    onPhaseChanged: {
        if (phase !== "signIn" && phase !== "idle")
            clearSecrets()
        if (contentItem)
            contentItem.contentY = 0
        finishFirstRun()
    }
    onBusyChanged: finishFirstRun()
    onVisibleChanged: if (!visible) clearSecrets()
    Keys.onEscapePressed: event => {
        if (firstRun) {
            cancelFlow()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    Theme { id: theme }

    ColumnLayout {
        id: flowContent
        width: root.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Image {
                objectName: "nuvioAccountFlowIcon"
                source: "../assets/icons/nuvio-official.png"
                sourceSize: Qt.size(38, 38)
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Nuvio")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 30
                Accessible.role: Accessible.Heading
            }
        }
        FlowText {
            text: {
                if (!root.account || !root.account.available)
                    return qsTr("Nuvio is unavailable for this profile.")
                if (root.firstRun && root.awaitingCompletion && root.busy)
                    return qsTr("Bringing in your library…")
                if (root.phase === "profiles")
                    return qsTr("Choose the Nuvio profile you want to bring into Colosseum.")
                if (root.phase === "preview")
                    return qsTr("Review what will be brought into Colosseum before you continue.")
                if (root.reconnect)
                    return qsTr("Sign in again to reconnect your Nuvio account on this device.")
                if (root.connected) {
                    if (root.primaryProvider === "nuvio")
                        return qsTr("Connected · Primary sync service")
                    if (root.primaryProvider === "stremio")
                        return qsTr("Connected · Stremio is the primary sync service")
                    return qsTr("Connected")
                }
                return qsTr("Bring your Nuvio library, watched items and addons into this profile.")
            }
        }
        FlowText {
            visible: text.length > 0
            text: root.account ? root.account.message : ""
            color: theme.gold
        }
        BusyIndicator {
            visible: root.busy
            running: visible
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Accessible.name: qsTr("Working")
        }
        AccountField {
            id: emailInput
            objectName: "nuvioEmail"
            visible: root.showSignIn
            enabled: !root.busy
            placeholderText: qsTr("Nuvio email")
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
            onAccepted: passwordInput.forceActiveFocus()
        }
        AccountField {
            id: passwordInput
            objectName: "nuvioPassword"
            visible: root.showSignIn
            enabled: !root.busy
            placeholderText: qsTr("Nuvio password")
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            onAccepted: root.signIn()
        }
        FlowButton {
            id: signInButton
            objectName: "nuvioSignIn"
            visible: root.showSignIn
            enabled: !root.busy && emailInput.text.trim().length > 0 && passwordInput.text.length > 0
            highlighted: true
            text: qsTr("Sign in to Nuvio")
            onClicked: root.signIn()
        }
        Repeater {
            model: root.phase === "profiles" && root.account ? root.account.profiles : []
            delegate: FlowButton {
                id: profileButton
                required property var modelData
                objectName: "nuvioProfile_" + modelData.index
                text: modelData.locked
                      ? qsTr("%1 · Locked profiles cannot be imported").arg(modelData.name)
                      : modelData.name
                enabled: !root.busy && !modelData.locked
                onClicked: {
                    if (root.firstRun)
                        root.account.previewProfile(modelData.index, true)
                    else
                        root.account.previewProfile(modelData.index)
                }
            }
        }
        ImportPreview {
            visible: root.phase === "preview"
            Layout.fillWidth: true
            rows: visible && root.account ? root.account.preview : []
        }
        FlowText {
            visible: root.phase === "preview" && root.primaryProvider === "stremio"
                && !root.previewBecomesPrimary
            text: qsTr("Imported library and watched items will also appear in Stremio while it is your primary sync service.")
        }
        FlowText {
            visible: root.phase === "preview" && root.previewBecomesPrimary
            text: qsTr("Nuvio will become the primary service for your library, watched items and playback progress.")
        }
        FlowButton {
            objectName: "nuvioApplyImport"
            visible: root.phase === "preview"
            enabled: !root.busy
            highlighted: true
            text: root.firstRun ? qsTr("Import and continue")
                  : root.primaryProvider === "nuvio" ? qsTr("Apply sync")
                  : root.previewBecomesPrimary ? qsTr("Use Nuvio as primary")
                  : qsTr("Import into Colosseum")
            onClicked: {
                root.awaitingCompletion = root.firstRun
                if (root.firstRun)
                    root.account.apply(true)
                else
                    root.account.apply()
            }
        }
        FlowButton {
            objectName: "nuvioCancelImport"
            visible: root.firstRun || root.phase === "preview" || root.phase === "profiles"
            enabled: root.phase !== "preview" || !root.busy
            text: qsTr("Cancel")
            onClicked: root.cancelFlow()
        }
        FlowButton {
            objectName: "nuvioLoadProfiles"
            visible: root.connected && !root.reconnect && (root.phase === "idle" || root.phase === "done")
            enabled: !root.busy
            highlighted: root.primaryProvider !== "nuvio"
            text: qsTr("Choose a Nuvio profile")
            onClicked: root.account.loadProfiles()
        }
        FlowButton {
            objectName: "nuvioSyncNow"
            visible: root.connected && !root.reconnect && !root.firstRun
                     && root.primaryProvider === "nuvio" && (root.phase === "idle" || root.phase === "done")
            enabled: !root.busy
            highlighted: true
            text: qsTr("Sync now")
            onClicked: root.account.syncNow()
        }
        FlowButton {
            objectName: "nuvioMakePrimary"
            visible: root.connected && !root.reconnect && !root.firstRun
                     && root.primaryProvider !== "nuvio" && (root.phase === "idle" || root.phase === "done")
            enabled: !root.busy
            text: qsTr("Use Nuvio as primary")
            onClicked: root.account.makePrimary()
        }
        FlowButton {
            objectName: "nuvioDisconnect"
            visible: root.connected && !root.firstRun
                     && (root.phase === "idle" || root.phase === "done" || root.reconnect)
            enabled: !root.busy
            text: qsTr("Disconnect Nuvio")
            onClicked: root.account.disconnect()
        }
    }

    component FlowText: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 13
    }

    component FlowButton: Button {
        id: flowButton
        Layout.fillWidth: true
        Layout.minimumHeight: 44
        font.family: theme.ui
        font.pixelSize: 13
        palette.button: theme.glassTint
        palette.buttonText: theme.ink
        palette.highlight: theme.gold
        palette.highlightedText: "#17130a"
        contentItem: Text {
            text: flowButton.text
            font: flowButton.font
            color: flowButton.highlighted ? flowButton.palette.highlightedText : theme.ink
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component AccountField: TextField {
        id: accountField
        Layout.fillWidth: true
        Layout.minimumHeight: 44
        selectByMouse: true
        color: theme.ink
        placeholderTextColor: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 14
        Accessible.name: placeholderText
        background: Rectangle {
            color: theme.glassTint
            radius: 9
            border.color: accountField.activeFocus ? theme.gold : theme.edge
        }
    }

    component ImportPreview: Column {
        id: previewRows
        required property var rows
        spacing: 8
        Theme { id: previewTheme }

        function categoryLabel(category) {
            switch (category) {
            case "library": return qsTr("Library")
            case "movies": return qsTr("Watched movies")
            case "episodes": return qsTr("Watched episodes")
            case "progress": return qsTr("Playback progress")
            case "addons": return qsTr("Addons")
            default: return qsTr("Other items")
            }
        }

        Repeater {
            model: previewRows.rows
            delegate: Column {
                id: previewRow
                required property var modelData
                width: previewRows.width
                spacing: 4

                Text {
                    width: previewRow.width
                    text: previewRows.categoryLabel(previewRow.modelData.category)
                    color: previewTheme.ink
                    font.family: previewTheme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    width: previewRow.width
                    text: qsTr("%1 matched · %2 unresolved · %3 already present · %4 skipped")
                          .arg(previewRow.modelData.matched).arg(previewRow.modelData.unresolved)
                          .arg(previewRow.modelData.existing).arg(previewRow.modelData.skipped)
                    color: previewTheme.inkDim
                    font.family: previewTheme.ui
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
