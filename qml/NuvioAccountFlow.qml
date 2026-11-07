import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "account" as Account

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
    readonly property bool deviceSignInStep: phase === "deviceCode" || phase === "deviceCodeExpired"
    property bool awaitingCompletion: false

    implicitHeight: flowContent.implicitHeight
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: firstRun ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded

    function takeKeyboardFocus() {
        if (pinInput.visible && pinInput.enabled)
            pinInput.forceActiveFocus(Qt.TabFocusReason)
        else if (deviceBackButton.visible)
            deviceBackButton.forceActiveFocus(Qt.TabFocusReason)
        else if (emailInput.visible && emailInput.enabled)
            emailInput.forceActiveFocus(Qt.TabFocusReason)
    }

    function clearSecrets() {
        emailInput.clear()
        passwordInput.clear()
        pinInput.clear()
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

    function startDeviceSignIn() {
        if (busy || !(showSignIn || phase === "deviceCodeExpired"))
            return
        clearSecrets()
        account.startDeviceSignIn()
    }

    function submitPin() {
        if (!unlockButton.visible || !unlockButton.enabled)
            return
        const pin = pinInput.text
        pinInput.clear()
        account.unlockProfile(pin)
    }

    function backFromPin() {
        pinInput.clear()
        account.cancelPin()
    }

    function backFromSignInStep() {
        if (phase === "pin") {
            backFromPin()
            return true
        }
        if (!deviceSignInStep)
            return false
        account.cancelDeviceSignIn()
        Qt.callLater(root.takeKeyboardFocus)
        return true
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
        if (phase === "pin" || deviceSignInStep)
            Qt.callLater(root.takeKeyboardFocus)
        finishFirstRun()
    }
    onBusyChanged: {
        if (phase === "pin" && !busy)
            Qt.callLater(root.takeKeyboardFocus)
        finishFirstRun()
    }
    onVisibleChanged: {
        if (!visible)
            clearSecrets()
        else if (phase === "pin" || deviceSignInStep)
            Qt.callLater(root.takeKeyboardFocus)
    }
    Keys.onShortcutOverride: event => {
        if (deviceSignInStep && (event.key === Qt.Key_Escape || event.key === Qt.Key_Back))
            event.accepted = true
    }
    Keys.onEscapePressed: event => {
        event.accepted = backFromSignInStep()
        if (!event.accepted && firstRun) {
            cancelFlow()
            event.accepted = true
        }
    }
    Keys.onBackPressed: event => { event.accepted = backFromSignInStep() }

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
            id: flowDescription
            objectName: "nuvioFlowDescription"
            readonly property string profileName: {
                if (!root.account || !root.account.available || (root.firstRun && root.awaitingCompletion && root.busy))
                    return ""
                if (root.phase === "pin" || root.phase === "preview")
                    return root.account.selectedProfileName ?? ""
                if (root.connected && !root.reconnect && (root.phase === "idle" || root.phase === "done"))
                    return root.account.connectedProfileName ?? ""
                return ""
            }
            textFormat: Text.PlainText
            color: profileName.length > 0 ? theme.ink : theme.inkDim
            font.pixelSize: profileName.length > 0 ? 15 : 13
            font.weight: profileName.length > 0 ? Font.DemiBold : Font.Normal
            text: {
                if (!root.account || !root.account.available)
                    return qsTr("Nuvio is unavailable for this profile.")
                if (root.firstRun && root.awaitingCompletion && root.busy)
                    return qsTr("Bringing in your library…")
                if (root.phase === "deviceCode")
                    return deviceCodeText.text.length > 0
                        ? qsTr("Open the link in a browser and enter this code.")
                        : qsTr("Preparing a sign-in code…")
                if (root.phase === "deviceCodeExpired")
                    return qsTr("Get a new code to continue signing in.")
                if (root.phase === "profiles")
                    return qsTr("Choose the Nuvio profile you want to bring into Colosseum.")
                if (root.phase === "pin")
                    return flowDescription.profileName.length > 0
                        ? qsTr("Enter the four-digit PIN for %1.").arg(flowDescription.profileName)
                        : qsTr("Enter this Nuvio profile’s four-digit PIN.")
                if (root.phase === "preview")
                    return flowDescription.profileName.length > 0
                        ? qsTr("Review import from %1").arg(flowDescription.profileName)
                        : qsTr("Review your import")
                if (root.reconnect)
                    return qsTr("Sign in again to reconnect your Nuvio account on this device.")
                if (root.connected) {
                    const profileName = flowDescription.profileName
                    if (profileName.length > 0) {
                        if (root.primaryProvider === "nuvio")
                            return qsTr("Connected to %1 · Primary sync service").arg(profileName)
                        if (root.primaryProvider === "stremio")
                            return qsTr("Connected to %1 · Stremio is the primary sync service").arg(profileName)
                        return qsTr("Connected to %1").arg(profileName)
                    }
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
        FlowButton {
            objectName: "nuvioStartDeviceSignIn"
            visible: root.showSignIn
            enabled: !root.busy
            text: qsTr("Sign in with a code")
            onClicked: root.startDeviceSignIn()
        }
        FlowText {
            id: deviceCodeText
            objectName: "nuvioDeviceUserCode"
            visible: text.length > 0
            text: root.phase === "deviceCode" && root.account ? root.account.deviceUserCode : ""
            textFormat: Text.PlainText
            color: theme.gold
            font.pixelSize: 28
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: qsTr("Nuvio sign-in code %1").arg(text)
        }
        FlowButton {
            objectName: "nuvioDeviceLink"
            visible: deviceCodeText.visible
            variant: "link"
            text: "nuvio.tv/link"
            Accessible.name: qsTr("Open nuvio.tv/link in a browser")
            onClicked: Qt.openUrlExternally("https://nuvio.tv/link")
        }
        FlowButton {
            objectName: "nuvioRetryDeviceCode"
            visible: root.phase === "deviceCodeExpired"
            enabled: !root.busy
            highlighted: true
            text: qsTr("Get a new code")
            onClicked: root.startDeviceSignIn()
        }
        FlowButton {
            id: deviceBackButton
            objectName: "nuvioDeviceSignInBack"
            visible: root.deviceSignInStep
            variant: "link"
            text: qsTr("Back to email sign-in")
            onClicked: root.backFromSignInStep()
        }
        Repeater {
            model: root.phase === "profiles" && root.account ? root.account.profiles : []
            delegate: FlowButton {
                id: profileButton
                required property var modelData
                objectName: "nuvioProfile_" + modelData.index
                text: {
                    if (modelData.current && modelData.locked)
                        return qsTr("%1 · Current profile · Locked").arg(modelData.name)
                    if (modelData.current)
                        return qsTr("%1 · Current profile").arg(modelData.name)
                    if (modelData.locked)
                        return qsTr("%1 · Locked").arg(modelData.name)
                    return modelData.name
                }
                enabled: !root.busy
                onClicked: {
                    if (root.firstRun)
                        root.account.previewProfile(modelData.index, true)
                    else
                        root.account.previewProfile(modelData.index)
                }
            }
        }
        AccountField {
            id: pinInput
            objectName: "nuvioPin"
            visible: root.phase === "pin"
            enabled: !root.busy
            placeholderText: qsTr("Profile PIN")
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            maximumLength: 4
            validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
            onAccepted: root.submitPin()
        }
        FlowButton {
            id: unlockButton
            objectName: "nuvioUnlockProfile"
            visible: root.phase === "pin"
            enabled: !root.busy && pinInput.acceptableInput
            highlighted: true
            text: qsTr("Unlock profile")
            onClicked: root.submitPin()
        }
        FlowButton {
            objectName: "nuvioPinBack"
            visible: root.phase === "pin"
            variant: "link"
            text: qsTr("Back to profiles")
            onClicked: root.backFromPin()
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
            text: qsTr("Nuvio will become your primary Theatre sync service.")
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
            variant: "link"
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

    component FlowButton: Account.AccountButton {
        id: flowButton
        Layout.fillWidth: true
        Layout.minimumHeight: Math.max(44, contentItem.implicitHeight + 16)
        variant: highlighted ? "primary" : "secondary"
        contentItem: Text {
            text: flowButton.text
            textFormat: Text.PlainText
            color: flowButton.highlighted ? flowButton.primaryInk : theme.ink
            opacity: flowButton.enabled ? 1 : 0.58
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
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

    component ImportPreview: Rectangle {
        id: previewRows
        required property var rows
        readonly property var populatedRows: (rows || []).filter(row =>
            row.matched > 0 || row.existing > 0 || row.unresolved > 0 || row.skipped > 0)
        implicitHeight: entries.implicitHeight + 20
        radius: 10
        color: Qt.rgba(0, 0, 0, 0.18)
        border.color: previewTheme.edge
        Theme { id: previewTheme }

        function categoryLabel(category) {
            switch (category) {
            case "library": return qsTr("Library")
            case "movies": return qsTr("Watched movies")
            case "episodes": return qsTr("Watched episodes")
            case "progress": return qsTr("Playback progress")
            case "addons": return qsTr("Add-ons")
            default: return qsTr("Other items")
            }
        }

        ColumnLayout {
            id: entries
            x: 14
            y: 10
            width: Math.max(0, previewRows.width - 2 * x)
            spacing: 8

            Text {
                Layout.fillWidth: true
                visible: previewRows.populatedRows.length === 0
                text: qsTr("There’s nothing to import from this profile yet.")
                color: previewTheme.inkDim
                font.family: previewTheme.ui
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Repeater {
                model: previewRows.populatedRows
                delegate: ColumnLayout {
                    id: entry
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 4

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        visible: entry.index > 0
                        color: previewTheme.edge
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: previewRows.categoryLabel(entry.modelData.category)
                            color: previewTheme.ink
                            font.family: previewTheme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }
                        Text {
                            visible: entry.modelData.matched > 0
                            Layout.maximumWidth: entries.width * 0.45
                            text: qsTr("%n to import", "Importable item count", entry.modelData.matched)
                            color: previewTheme.gold
                            font.family: previewTheme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            wrapMode: Text.Wrap
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: {
                            const counts = entry.modelData
                            const notes = []
                            if (counts.existing > 0)
                                notes.push(qsTr("%n already in Colosseum", "Existing item count", counts.existing))
                            if (counts.unresolved > 0)
                                notes.push(qsTr("%n couldn’t be matched", "Unmatched item count", counts.unresolved))
                            if (counts.skipped > 0)
                                notes.push(qsTr("%n skipped", "Skipped item count", counts.skipped))
                            return notes.join(qsTr(" · ", "Import detail separator"))
                        }
                        color: previewTheme.inkDim
                        font.family: previewTheme.ui
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
