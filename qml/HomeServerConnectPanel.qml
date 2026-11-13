pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "account" as Account

Popup {
    id: root
    objectName: "homeServerConnectPanel"
    property var service: typeof HomeServers !== "undefined" ? HomeServers : null
    readonly property string provider: providerChoice.currentIndex === 0 ? "jellyfin" : providerChoice.currentIndex === 1 ? "emby" : "plex"
    readonly property string providerName: providerChoice.currentIndex === 0 ? qsTr("Jellyfin") : providerChoice.currentIndex === 1 ? qsTr("Emby") : qsTr("Plex")
    readonly property bool insecure: !!service && service.isInsecureAddress(addressField.text)
    property var openedGeneration: -1
    property var sessionService: null
    signal dismissed()
    parent: Overlay.overlay
    width: Math.min(520, parent ? parent.width - 24 : 520)
    height: Math.min(implicitHeight, parent ? parent.height - 24 : implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    palette.text: theme.ink
    palette.windowText: theme.ink
    palette.buttonText: theme.ink
    palette.highlight: theme.gold
    background: Rectangle { color: theme.biblioWashTop; radius: 20; border.color: theme.edge }
    Theme { id: theme }

    component ConsentCheck: CheckBox {
        id: control
        implicitHeight: Math.max(44, contentItem.implicitHeight + 12)
        spacing: 12
        indicator: Rectangle {
            implicitWidth: 20; implicitHeight: 20
            x: 0; y: (control.height - height) / 2
            radius: 5
            color: control.checked ? theme.gold : "transparent"
            border.color: control.checked || control.activeFocus ? theme.gold : theme.edge
            Text {
                anchors.centerIn: parent
                text: control.checked ? "✓" : ""
                color: theme.biblioWashBottom
                font.pixelSize: 14
            }
        }
        contentItem: Label {
            leftPadding: control.indicator.width + control.spacing
            text: control.text
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(12)
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: "transparent"
            radius: 8
            border.width: control.activeFocus ? 1 : 0
            border.color: theme.gold
        }
    }

    function chooseProvider(index) {
        if (providerChoice.currentIndex === index) return
        providerChoice.currentIndex = index
        root.resetSecrets()
        if (root.service) root.service.cancelSignIn()
    }

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

    contentItem: ColumnLayout {
        spacing: 12
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Back) {
                root.close()
                event.accepted = true
            }
        }
        ScrollView {
            id: scroll
            objectName: "homeServerFormScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            implicitHeight: form.implicitHeight
            contentWidth: availableWidth
        ColumnLayout {
            id: form
            width: scroll.availableWidth
            spacing: 12
            Label { text: qsTr("Connect a home server"); color: theme.ink; font.family: theme.display; font.pixelSize: theme.fontSize(24); Layout.fillWidth: true; wrapMode: Text.Wrap }
            RowLayout {
                id: providerChoice
                objectName: "homeServerProvider"
                Layout.fillWidth: true
                property int currentIndex: 0
                spacing: 10
                Accessible.name: qsTr("Server provider")
                Keys.onPressed: event => {
                    var next = currentIndex
                    if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) next = Math.min(2, currentIndex + 1)
                    else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) next = Math.max(0, currentIndex - 1)
                    else return
                    root.chooseProvider(next)
                    providerTiles.itemAt(next).forceActiveFocus()
                    event.accepted = true
                }
                Repeater {
                    id: providerTiles
                    objectName: "homeServerProviderTiles"
                    model: ["jellyfin", "emby", "plex"]
                    delegate: Button {
                        id: providerTile
                        required property string modelData
                        required property int index
                        objectName: "homeServerProvider_" + modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        implicitHeight: 54
                        padding: root.width < 400 ? 6 : 8
                        checkable: true
                        autoExclusive: true
                        checked: providerChoice.currentIndex === index
                        Accessible.name: index === 0 ? qsTr("Jellyfin") : index === 1 ? qsTr("Emby") : qsTr("Plex")
                        onClicked: root.chooseProvider(index)
                        background: Rectangle {
                            radius: 14
                            color: providerTile.checked ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.07)
                                : providerTile.hovered ? theme.glassHi : Qt.rgba(1, 1, 1, 0.035)
                            border.color: providerTile.checked || providerTile.activeFocus ? theme.gold : theme.edge
                            border.width: providerTile.activeFocus ? 2 : 1
                        }
                        contentItem: RowLayout {
                            spacing: root.width < 400 ? 4 : 8
                            Image {
                                objectName: "homeServerProviderLogo_" + providerTile.modelData
                                Layout.preferredWidth: root.width < 400 ? 20 : 24
                                Layout.preferredHeight: root.width < 400 ? 20 : 24
                                source: "../assets/icons/servers/" + providerTile.modelData + ".svg"
                                sourceSize: Qt.size(48, 48)
                                fillMode: Image.PreserveAspectFit
                                Accessible.ignored: true
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: providerTile.Accessible.name
                                color: providerTile.checked ? theme.ink : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: theme.fontSize(12)
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                visible: root.provider !== "plex"
                Layout.fillWidth: true
                spacing: 12
                Account.AccountField {
                    id: addressField
                    controlObjectName: "homeServerAddress"
                    Layout.fillWidth: true
                    label: qsTr("Server address")
                    placeholderText: qsTr("https://server.example.com")
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onTextChanged: httpConsent.checked = false
                }
                Account.AccountField {
                    id: userField
                    controlObjectName: "homeServerUsername"
                    Layout.fillWidth: true
                    label: qsTr("Username")
                    placeholderText: qsTr("Username")
                    Accessible.name: placeholderText
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
                Account.AccountField {
                    id: passwordField
                    controlObjectName: "homeServerPassword"
                    Layout.fillWidth: true
                    label: qsTr("Password")
                    placeholderText: qsTr("Password")
                    Accessible.name: placeholderText
                    password: true
                    onTextChanged: {
                        const singleLine = passwordField.text.replace(/[\r\n]/g, "")
                        if (passwordField.text !== singleLine) passwordField.text = singleLine
                    }
                    onAccepted: root.submit()
                }
                Label {
                    visible: root.insecure
                    Layout.fillWidth: true
                    text: qsTr("HTTP sends sign-in and media without encryption.")
                    wrapMode: Text.Wrap
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                }
                ConsentCheck {
                    id: httpConsent
                    objectName: "homeServerHttpConsent"
                    visible: root.insecure
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Allow this HTTP connection")
                }
            }
            ColumnLayout {
                visible: root.provider === "plex"
                Layout.fillWidth: true
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Approve sign-in in your browser, then choose a server.")
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.fontSize(12); wrapMode: Text.Wrap
                }
                Account.AccountButton {
                    objectName: "homeServerBeginPlex"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Sign in with Plex")
                    variant: "primary"
                    enabled: !!root.service && root.service.available && !root.service.busy
                    onClicked: root.service.beginPlex()
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 104
                    visible: !!root.service && root.service.plexCode.length > 0
                    radius: 14
                    color: Qt.rgba(1, 1, 1, 0.035)
                    border.color: theme.edge
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 32
                        spacing: 10
                        Label {
                            width: parent.width
                            text: qsTr("Enter this code in Plex")
                            horizontalAlignment: Text.AlignHCenter
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                        }
                        Label {
                            objectName: "homeServerPlexCode"
                            width: parent.width
                            text: root.service ? root.service.plexCode : ""
                            textFormat: Text.PlainText
                            horizontalAlignment: Text.AlignHCenter
                            color: theme.gold
                            font.family: theme.hud
                            font.pixelSize: theme.fontSize(30)
                            font.weight: Font.DemiBold
                            font.letterSpacing: 6
                        }
                    }
                }
                Account.AccountButton {
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
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                }
                ConsentCheck {
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
                    delegate: Account.AccountButton {
                        id: plexCandidate
                        required property var modelData
                        objectName: "homeServerPlexSelect_" + plexCandidate.modelData.id
                        Layout.fillWidth: true
                        Layout.minimumHeight: 44
                        text: plexCandidate.modelData.name
                        contentItem: Label {
                            text: plexCandidate.text
                            textFormat: Text.PlainText
                            color: plexCandidate.enabled ? theme.ink : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: theme.fontSize(13)
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 16; rightPadding: 16
                            elide: Text.ElideRight
                        }
                        enabled: plexCandidate.modelData.available && !root.service.busy
                                 && (!root.service.httpConsentRequired || plexHttpConsent.checked)
                        onClicked: root.service.selectPlexServer(plexCandidate.modelData.id, plexHttpConsent.checked)
                    }
                }
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
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: theme.fontSize(12)
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Account.AccountButton {
                objectName: "homeServerCancel"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitWidth: 110
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            Account.AccountButton {
                objectName: "homeServerSubmit"
                visible: root.provider !== "plex"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitWidth: 180
                variant: "primary"
                text: root.service && root.service.busy ? qsTr("Connecting…") : qsTr("Connect")
                enabled: !!root.service && root.service.available && !root.service.busy
                    && addressField.text.trim().length > 0 && userField.text.trim().length > 0
                    && (!root.insecure || httpConsent.checked)
                onClicked: root.submit()
            }
        }
    }
}
