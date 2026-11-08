pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Rectangle {
    id: card
    objectName: "trackerHomeServersCard"
    property var controller: null
    property bool expanded: false
    property string removeId: ""
    property string localNotice: ""
    readonly property bool usable: !!controller && controller.available
    readonly property var serverRows: controller ? controller.connections : []

    Layout.fillWidth: true
    implicitHeight: cardBody.implicitHeight + 36
    radius: 16
    color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.96)
    border.width: 1
    border.color: theme.edge

    Theme { id: theme }

    component ServerButton: Basic.Button {
        id: serverButton
        property bool prominent: false
        implicitHeight: 36
        implicitWidth: Math.max(86, buttonText.implicitWidth + 24)
        activeFocusOnTab: true
        background: Rectangle {
            radius: 9
            border.width: serverButton.activeFocus ? 2 : 1
            border.color: serverButton.prominent ? theme.gold : theme.edge
            color: serverButton.down || serverButton.hovered
                ? theme.glassHi : theme.glassTint
            opacity: serverButton.enabled ? 1 : 0.45
        }
        contentItem: Text {
            id: buttonText
            text: serverButton.text
            color: serverButton.prominent ? theme.gold : theme.ink
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component.onDestruction: {
        if (card.controller && card.controller.busy)
            card.controller.cancelConnect()
        card.clearInputs()
    }

    function clearInputs() {
        serverName.text = ""
        serverAddress.text = ""
        serverUser.text = ""
        serverPassword.text = ""
    }

    Connections {
        target: card.controller
        function onProfileChanged() {
            card.expanded = false
            card.removeId = ""
            removeDialog.close()
            card.localNotice = ""
            card.clearInputs()
        }
        function onStatusChanged() {
            if (!card.controller || card.controller.busy
                    || card.controller.hasError) return
            if (card.controller.notice === "Connected to your server.") {
                card.expanded = false
                card.clearInputs()
            }
        }
    }

    ColumnLayout {
        id: cardBody
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Home servers")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Play your Jellyfin or Emby copies in Colosseum.")
                    wrapMode: Text.WordWrap
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 11
                }
            }
            ServerButton {
                id: addServerButton
                objectName: "trackerHomeServerAddButton"
                text: card.expanded ? qsTr("Close") : qsTr("Add server")
                prominent: true
                enabled: card.usable && (!card.controller.busy || card.expanded)
                Accessible.name: text
                onClicked: {
                    if (card.expanded) {
                        if (card.controller.busy)
                            card.controller.cancelConnect()
                        card.clearInputs()
                    }
                    card.expanded = !card.expanded
                }
            }
        }

        Text {
            visible: !card.usable
            Layout.fillWidth: true
            text: qsTr("Secure connections are unavailable in this profile.")
            wrapMode: Text.WordWrap
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
        }

        Repeater {
            model: card.serverRows
            delegate: Rectangle {
                id: serverRow
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: serverRowContent.implicitHeight + 20
                radius: 10
                color: theme.glassTint
                border.width: 1
                border.color: theme.edge

                ColumnLayout {
                    id: serverRowContent
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: serverRow.modelData.name || qsTr("Your server")
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (serverRow.modelData.kind || "") + " · "
                                      + (serverRow.modelData.address || "")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                            }
                        }
                        Switch {
                            objectName: "trackerHomeServerEnabledSwitch"
                            checked: serverRow.modelData.enabled === true
                            enabled: card.usable && !card.controller.busy
                            Accessible.name: qsTr("Enable") + " " + serverRow.modelData.name
                            onToggled: {
                                if (!card.controller.setConnectionEnabled(
                                            serverRow.modelData.id, checked))
                                    card.localNotice = qsTr("Unable to update this server.")
                                else card.localNotice = ""
                            }
                        }
                        ServerButton {
                            objectName: "trackerHomeServerRemoveButton"
                            text: qsTr("Remove")
                            enabled: card.usable && !card.controller.busy
                            Accessible.name: qsTr("Remove") + " " + serverRow.modelData.name
                            onClicked: {
                                card.removeId = serverRow.modelData.id
                                removeDialog.open()
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            visible: card.expanded
            Layout.fillWidth: true
            spacing: 10

            ComboBox {
                id: serverKind
                objectName: "trackerHomeServerKind"
                Layout.fillWidth: true
                model: ["Jellyfin", "Emby"]
                Accessible.name: qsTr("Server type")
            }
            TextField {
                id: serverName
                objectName: "trackerHomeServerName"
                Layout.fillWidth: true
                placeholderText: qsTr("Server name (optional)")
                maximumLength: 96
                Accessible.name: qsTr("Server name")
            }
            TextField {
                id: serverAddress
                objectName: "trackerHomeServerAddress"
                Layout.fillWidth: true
                placeholderText: qsTr("https://server.example or http://192.168.1.2:8096")
                maximumLength: 1024
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Server address")
            }
            TextField {
                id: serverUser
                objectName: "trackerHomeServerUsername"
                Layout.fillWidth: true
                placeholderText: qsTr("Username")
                maximumLength: 320
                inputMethodHints: Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Server username")
            }
            TextField {
                id: serverPassword
                objectName: "trackerHomeServerPassword"
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                maximumLength: 1024
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Server password")
                onAccepted: {
                    if (connectServerButton.enabled)
                        connectServerButton.clicked()
                }
            }
            Text {
                visible: serverAddress.text.toLowerCase().startsWith("http://")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("HTTP sends your password without encryption. Use HTTPS outside your own trusted network.")
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ServerButton {
                    id: connectServerButton
                    objectName: "trackerHomeServerConnectButton"
                    text: card.controller && card.controller.busy
                          ? qsTr("Connecting…") : qsTr("Connect")
                    prominent: true
                    enabled: card.usable && !card.controller.busy
                             && serverAddress.text.trim().length > 0
                             && serverUser.text.trim().length > 0
                             && serverPassword.text.length > 0
                    Accessible.name: qsTr("Connect home server")
                    onClicked: {
                        if (!card.controller) return
                        const password = serverPassword.text
                        card.controller.connectServer(
                            serverKind.currentIndex === 0 ? "jellyfin" : "emby",
                            serverName.text, serverAddress.text,
                            serverUser.text, password)
                        // A password is needed only for the active request.
                        serverPassword.text = ""
                    }
                }
                ServerButton {
                    objectName: "trackerHomeServerCancelButton"
                    text: qsTr("Cancel")
                    enabled: true
                    onClicked: {
                        if (card.controller && card.controller.busy)
                            card.controller.cancelConnect()
                        card.expanded = false
                        card.clearInputs()
                    }
                }
            }
        }

        Text {
            visible: card.localNotice.length > 0
            Layout.fillWidth: true
            text: card.localNotice
            wrapMode: Text.WordWrap
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 11
        }
        Text {
            visible: !!card.controller && card.controller.notice.length > 0
            Layout.fillWidth: true
            text: card.controller ? card.controller.notice : ""
            color: card.controller && card.controller.hasError ? theme.gold : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: removeDialog
        objectName: "trackerHomeServerRemoveDialog"
        title: qsTr("Remove server?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: {
            if (card.controller && card.removeId.length > 0
                    && !card.controller.removeConnection(card.removeId))
                card.localNotice = qsTr("Unable to remove this server.")
            card.removeId = ""
        }
        onRejected: card.removeId = ""
        Text {
            text: qsTr("This removes the saved server connection and its token.")
            wrapMode: Text.WordWrap
            width: Math.min(340, card.width - 48)
        }
    }
}
