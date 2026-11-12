pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    objectName: "homeServerConnectPanel"
    property var service: typeof HomeServers !== "undefined" ? HomeServers : null
    readonly property string provider: providerChoice.currentIndex === 0 ? "jellyfin" : providerChoice.currentIndex === 1 ? "emby" : "plex"
    readonly property bool insecure: !!service && service.isInsecureAddress(addressField.text)
    property var openedGeneration: -1
    property var sessionService: null
    signal dismissed()
    parent: Overlay.overlay
    width: Math.min(480, parent ? parent.width - 24 : 480)
    height: Math.min(implicitHeight, parent ? parent.height - 24 : implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    palette.text: "#ede8dd"
    palette.windowText: "#ede8dd"
    palette.buttonText: "#ede8dd"
    palette.button: "#272729"
    palette.base: "#1b1c20"
    palette.highlight: "#9d8134"
    background: Rectangle { color: "#101115"; radius: 16; border.color: "#6d5f35" }

    function resetSecrets() {
        if (passwordField) passwordField.clear()
        if (httpConsent) httpConsent.checked = false
        if (plexHttpConsent) plexHttpConsent.checked = false
    }
    function submit() {
        if (!service || service.busy || !service.available || !addressField.text.trim()
                || !userField.text.trim() || (insecure && !httpConsent.checked)) return
        const secret = passwordField.text
        passwordField.clear()
        service.connectServer(provider, addressField.text.trim(), userField.text.trim(), secret, httpConsent.checked)
    }
    onOpened: {
        sessionService = service
        openedGeneration = service ? service.profileGeneration : -1
        providerChoice.forceActiveFocus()
    }
    onServiceChanged: {
        resetSecrets()
        if (visible) close()
    }
    onClosed: {
        resetSecrets()
        if (sessionService) sessionService.cancelSignIn()
        sessionService = null
        dismissed()
    }
    Component.onDestruction: if (visible && sessionService) sessionService.cancelSignIn()
    Connections {
        target: root.service
        ignoreUnknownSignals: true
        function onChanged() {
            if (root.visible && (!root.service.available || root.service.profileGeneration !== root.openedGeneration)) {
                root.resetSecrets()
                root.close()
            }
        }
        function onConnectionAdded(id) { if (root.visible) root.close() }
    }

    contentItem: ScrollView {
        id: scroll
        clip: true
        implicitHeight: form.implicitHeight
        contentWidth: availableWidth
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Back) {
                root.close()
                event.accepted = true
            }
        }
        ColumnLayout {
            id: form
            width: scroll.availableWidth
            spacing: 12
            Label { text: qsTr("Connect a home server"); color: "#ede8dd"; font.pixelSize: 22; Layout.fillWidth: true; wrapMode: Text.Wrap }
            ComboBox {
                id: providerChoice
                objectName: "homeServerProvider"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                model: ["Jellyfin", "Emby", "Plex"]
                Accessible.name: qsTr("Server provider")
                onActivated: {
                    root.resetSecrets()
                    if (root.service) root.service.cancelSignIn()
                }
            }
            ColumnLayout {
                visible: root.provider !== "plex"
                Layout.fillWidth: true
                spacing: 12
                TextField {
                    id: addressField
                    objectName: "homeServerAddress"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    placeholderText: qsTr("Server address")
                    Accessible.name: placeholderText
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onTextChanged: httpConsent.checked = false
                }
                TextField {
                    id: userField
                    objectName: "homeServerUsername"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    placeholderText: qsTr("Username")
                    Accessible.name: placeholderText
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
                TextField {
                    id: passwordField
                    objectName: "homeServerPassword"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    placeholderText: qsTr("Password")
                    Accessible.name: placeholderText
                    echoMode: TextInput.Password
                    onAccepted: root.submit()
                }
                Label {
                    visible: root.insecure
                    Layout.fillWidth: true
                    text: qsTr("HTTP sends your sign-in and media without encryption. Only continue on a network you trust.")
                    wrapMode: Text.Wrap
                    color: "#e5c36c"
                }
                CheckBox {
                    id: httpConsent
                    objectName: "homeServerHttpConsent"
                    visible: root.insecure
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Allow this HTTP connection")
                }
                Button {
                    objectName: "homeServerSubmit"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Connect")
                    enabled: !!root.service && root.service.available && !root.service.busy
                             && addressField.text.trim().length > 0 && userField.text.trim().length > 0
                             && (!root.insecure || httpConsent.checked)
                    onClicked: root.submit()
                }
            }
            ColumnLayout {
                visible: root.provider === "plex"
                Layout.fillWidth: true
                spacing: 12
                Button {
                    objectName: "homeServerBeginPlex"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Sign in with Plex")
                    enabled: !!root.service && root.service.available && !root.service.busy
                    onClicked: root.service.beginPlex()
                }
                Label {
                    objectName: "homeServerPlexCode"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.service ? root.service.plexCode : ""
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignHCenter
                    color: "#f0c44a"
                    font.pixelSize: 28
                    font.letterSpacing: 4
                }
                Button {
                    objectName: "homeServerPlexBrowser"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    visible: !!root.service && root.service.plexUrl.length > 0
                    text: qsTr("Open Plex sign-in")
                    onClicked: Qt.openUrlExternally(root.service.plexUrl)
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!root.service && root.service.httpConsentRequired
                    text: qsTr("This Plex server requires HTTP. Your media and connection credentials will be sent without encryption.")
                    wrapMode: Text.Wrap
                    color: "#e5c36c"
                }
                CheckBox {
                    id: plexHttpConsent
                    objectName: "homeServerPlexHttpConsent"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    visible: !!root.service && root.service.httpConsentRequired
                    text: qsTr("Allow this HTTP connection")
                }
                Repeater {
                    objectName: "homeServerPlexCandidates"
                    model: root.service ? root.service.plexServers : []
                    delegate: Button {
                        id: plexCandidate
                        required property var modelData
                        objectName: "homeServerPlexSelect_" + plexCandidate.modelData.id
                        Layout.fillWidth: true
                        Layout.minimumHeight: 44
                        text: plexCandidate.modelData.name
                        enabled: plexCandidate.modelData.available && !root.service.busy
                                 && (!root.service.httpConsentRequired || plexHttpConsent.checked)
                        onClicked: root.service.selectPlexServer(plexCandidate.modelData.id, plexHttpConsent.checked)
                    }
                }
            }
            Label {
                objectName: "homeServerStatus"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.service ? root.service.status : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: "#c5b47e"
            }
            Button {
                objectName: "homeServerCancel"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                text: qsTr("Cancel")
                onClicked: root.close()
            }
        }
    }
}
