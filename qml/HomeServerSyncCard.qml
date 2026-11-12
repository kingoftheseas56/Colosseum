pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "homeServerSyncCard"
    property var service: typeof HomeServers !== "undefined" ? HomeServers : null
    property bool panelOpen: false
    implicitWidth: 360
    implicitHeight: content.implicitHeight + 36
    radius: 16
    color: "#f506070b"
    border.color: "#353332"

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 400 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("Home servers"); color: "#ede8dd"; font.pixelSize: 24; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: qsTr("Jellyfin · Emby · Plex"); color: "#aaa69e"; wrapMode: Text.WordWrap; font.pixelSize: 12 }
            }
            Button {
                id: connectButton
                objectName: "homeServerConnectButton"
                Layout.alignment: Qt.AlignRight
                Layout.minimumHeight: 44
                text: qsTr("Connect")
                enabled: !!root.service && root.service.available
                onClicked: root.panelOpen = true
                palette.buttonText: "#f0c44a"
                background: Rectangle {
                    radius: 9
                    color: connectButton.down || connectButton.hovered ? "#282522" : "#171719"
                    border.width: connectButton.activeFocus ? 2 : 1
                    border.color: "#b69a4d"
                }
            }
        }
        Label {
            objectName: "homeServerCardStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.service ? root.service.status : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: "#c5b47e"
        }
        Repeater {
            model: root.service ? root.service.connections : []
            delegate: ColumnLayout {
                id: serverRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: serverRow.modelData.name
                    textFormat: Text.PlainText
                    color: "#ede8dd"
                    wrapMode: Text.Wrap
                    font.pixelSize: 15
                }
                Label {
                    Layout.fillWidth: true
                    text: serverRow.modelData.provider === "jellyfin" ? qsTr("Jellyfin")
                          : serverRow.modelData.provider === "emby" ? qsTr("Emby") : qsTr("Plex")
                    textFormat: Text.PlainText
                    color: "#aaa69e"
                    font.pixelSize: 12
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: serverRow.modelData.status || (serverRow.modelData.needsReconnect ? qsTr("Reconnect to continue") : "")
                    textFormat: Text.PlainText
                    color: "#c5b47e"
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Switch {
                        objectName: "homeServerEnabled_" + serverRow.modelData.id
                        Layout.minimumHeight: 44
                        text: qsTr("Enabled")
                        palette.windowText: "#ede8dd"
                        checked: !!serverRow.modelData.enabled
                        onToggled: if (root.service) root.service.setEnabled(serverRow.modelData.id, checked)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "homeServerRemove_" + serverRow.modelData.id
                        Layout.minimumHeight: 44
                        text: qsTr("Remove")
                        onClicked: if (root.service) root.service.remove(serverRow.modelData.id)
                    }
                }
            }
        }
    }
    Loader {
        id: panelLoader
        active: root.panelOpen
        sourceComponent: HomeServerConnectPanel {
            service: root.service
            onDismissed: root.panelOpen = false
            Component.onCompleted: open()
        }
    }
}
