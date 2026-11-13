pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "account" as Account

Rectangle {
    id: root
    objectName: "homeServerSyncCard"
    property var service: typeof HomeServers !== "undefined" ? HomeServers : null
    property bool panelOpen: false
    implicitWidth: 360
    implicitHeight: content.implicitHeight + 40
    radius: 16
    color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.96)
    border.color: theme.edge
    Theme { id: theme }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 20
        spacing: 18
        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 520 ? 2 : 1
            columnSpacing: 24
            rowSpacing: 16
            RowLayout {
                Layout.fillWidth: true
                spacing: 18
                Row {
                    spacing: 10
                    Repeater {
                        objectName: "homeServerLogoTrio"
                        model: ["jellyfin", "emby", "plex"]
                        delegate: Image {
                            required property string modelData
                            objectName: "homeServerLogo_" + modelData
                            width: 28; height: 36
                            source: "../assets/icons/servers/" + modelData + ".svg"
                            sourceSize: Qt.size(56, 72)
                            fillMode: Image.PreserveAspectFit
                            Accessible.ignored: true
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Home servers")
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: theme.fontSize(24)
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Jellyfin · Emby · Plex")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(11)
                        wrapMode: Text.Wrap
                    }
                }
            }
            Account.AccountButton {
                objectName: "homeServerConnectButton"
                Layout.alignment: Qt.AlignRight
                implicitWidth: 120
                text: qsTr("Connect")
                enabled: !!root.service && root.service.available
                onClicked: root.panelOpen = true
            }
        }
        Label {
            objectName: "homeServerCardStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.service ? root.service.status : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(12)
        }
        Repeater {
            model: root.service ? root.service.connections : []
            delegate: GridLayout {
                id: serverRow
                required property var modelData
                Layout.fillWidth: true
                columns: root.width >= 640 ? 2 : 1
                rowSpacing: 12
                columnSpacing: 24
                Rectangle { Layout.columnSpan: serverRow.columns; Layout.fillWidth: true; implicitHeight: 1; color: theme.edge }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Image {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 40
                        source: "../assets/icons/servers/" + serverRow.modelData.provider + ".svg"
                        sourceSize: Qt.size(68, 80)
                        fillMode: Image.PreserveAspectFit
                        Accessible.ignored: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            text: serverRow.modelData.name
                            textFormat: Text.PlainText
                            color: theme.ink
                            wrapMode: Text.Wrap
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(15)
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: serverRow.modelData.provider === "jellyfin" ? qsTr("Jellyfin")
                                : serverRow.modelData.provider === "emby" ? qsTr("Emby") : qsTr("Plex")
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(11)
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: serverRow.modelData.status || (serverRow.modelData.needsReconnect ? qsTr("Reconnect to continue") : "")
                            textFormat: Text.PlainText
                            color: theme.inkDim
                            wrapMode: Text.Wrap
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(12)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: serverRow.columns === 1
                    Layout.alignment: Qt.AlignRight
                    spacing: 8
                    Switch {
                        objectName: "homeServerEnabled_" + serverRow.modelData.id
                        Layout.minimumHeight: 44
                        text: root.width >= 380 ? qsTr("Enabled") : ""
                        Accessible.name: qsTr("Enable %1").arg(serverRow.modelData.name)
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(12)
                        palette.windowText: theme.inkDim
                        palette.highlight: theme.gold
                        checked: !!serverRow.modelData.enabled
                        onToggled: if (root.service) root.service.setEnabled(serverRow.modelData.id, checked)
                    }
                    Item { visible: serverRow.columns === 1; Layout.fillWidth: true }
                    Account.AccountButton {
                        objectName: "homeServerRefresh_" + serverRow.modelData.id
                        implicitWidth: 84
                        text: qsTr("Refresh")
                        enabled: !!serverRow.modelData.enabled && !serverRow.modelData.needsReconnect && !serverRow.modelData.refreshing
                        onClicked: if (root.service) root.service.refresh(serverRow.modelData.id)
                    }
                    Account.AccountButton {
                        objectName: "homeServerRemove_" + serverRow.modelData.id
                        implicitWidth: 84
                        variant: "link"
                        text: qsTr("Remove")
                        onClicked: if (root.service) root.service.remove(serverRow.modelData.id)
                    }
                }
            }
        }
    }
    Loader {
        active: root.panelOpen
        sourceComponent: HomeServerConnectPanel {
            service: root.service
            onDismissed: root.panelOpen = false
            Component.onCompleted: open()
        }
    }
}
