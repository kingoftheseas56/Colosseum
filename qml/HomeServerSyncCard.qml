pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "account" as Account

Rectangle {
    id: root
    objectName: "homeServerSyncCard"
    property var service: typeof HomeServers !== "undefined" ? HomeServers : null
    property string provider: ""
    readonly property string providerName: provider === "jellyfin" ? qsTr("Jellyfin") : provider === "emby" ? qsTr("Emby") : qsTr("Plex")
    readonly property var providerConnections: service ? (service.connections || []).filter(row => !root.provider || row.provider === root.provider) : []
    readonly property string providerStatus: {
        if (!root.provider) return root.service ? root.service.status : ""
        if (!root.service || !root.service.available) return qsTr("Unavailable for this profile")
        if (!root.providerConnections.length) return qsTr("Not connected")
        const enabled = root.providerConnections.filter(row => row.enabled !== false)
        if (!enabled.length) return qsTr("Connections disabled")
        const unavailable = enabled.filter(row => row.offline || row.state === "unavailable")
        if (unavailable.length) return unavailable.length === 1 ? qsTr("1 server unavailable") : qsTr("%1 servers unavailable").arg(unavailable.length)
        const reconnect = enabled.filter(row => row.needsReconnect || row.state === "needsReconnect")
        if (reconnect.length) return reconnect.length === 1 ? qsTr("1 server needs sign-in") : qsTr("%1 servers need sign-in").arg(reconnect.length)
        if (enabled.some(row => row.refreshing || row.state === "refreshing")) return qsTr("Refreshing…")
        return root.providerConnections.length === 1 ? qsTr("1 saved connection")
            : qsTr("%1 saved connections").arg(root.providerConnections.length)
    }
    readonly property string providerStatusKind: {
        if (!root.service || !root.service.available) return "attention"
        const enabled = root.providerConnections.filter(row => row.enabled !== false)
        if (enabled.some(row => row.needsReconnect || row.state === "needsReconnect"
                                || row.offline || row.state === "unavailable")) return "attention"
        if (enabled.some(row => row.refreshing || row.state === "refreshing")
                || (root.panelOpen && root.service.busy)) return "busy"
        if (!root.providerConnections.length || !enabled.length) return "unlinked"
        return "linked"
    }
    property bool panelOpen: false
    implicitWidth: 360
    implicitHeight: content.implicitHeight + 40
    radius: 16
    color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.96)
    border.color: theme.edge
    Theme { id: theme }

    function focusConnectAction() {
        if (connectButton.visible && connectButton.enabled)
            connectButton.forceActiveFocus(Qt.TabFocusReason)
    }

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
                    visible: !root.provider
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
                Image {
                    objectName: "homeServerProviderCardLogo_" + root.provider
                    visible: root.provider.length > 0
                    Layout.preferredWidth: 42; Layout.preferredHeight: 42
                    source: "../assets/icons/servers/" + root.provider + ".svg"
                    sourceSize: Qt.size(84, 84); fillMode: Image.PreserveAspectFit
                    Accessible.ignored: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        objectName: "homeServerProviderCardName_" + root.provider
                        Layout.fillWidth: true
                        text: root.provider ? root.providerName : qsTr("Home servers")
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: theme.fontSize(root.provider ? 20 : 24)
                        font.weight: root.provider ? Font.DemiBold : Font.Normal
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "homeServerProviderCardDescription"
                        Layout.fillWidth: true
                        visible: !root.provider
                        text: qsTr("Jellyfin · Emby · Plex")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(11)
                        wrapMode: Text.Wrap
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: root.width < 520
                Layout.alignment: Qt.AlignRight
                spacing: 7
                Rectangle {
                    id: providerStatusBadge
                    objectName: "homeServerProviderStatusBadge_" + root.provider
                    visible: root.provider.length > 0
                    Layout.alignment: Qt.AlignRight
                    implicitWidth: providerStatusRow.implicitWidth + 20
                    Layout.preferredHeight: 28
                    radius: 9
                    color: root.providerStatusKind === "linked"
                           ? Qt.rgba(theme.vaultHealthy.r, theme.vaultHealthy.g, theme.vaultHealthy.b, 0.10)
                           : root.providerStatusKind === "attention"
                             ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.09) : theme.glassTint
                    border.width: 1
                    border.color: root.providerStatusKind === "linked" ? theme.vaultHealthy
                                  : root.providerStatusKind === "attention" ? theme.gold : theme.edge
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.providerName + qsTr(" status: ") + root.providerStatus
                    ToolTip.text: Accessible.name
                    ToolTip.delay: 350
                    ToolTip.visible: visible && statusHover.hovered
                    HoverHandler { id: statusHover }
                    RowLayout {
                        id: providerStatusRow
                        anchors.centerIn: parent
                        spacing: 6
                        Item {
                            objectName: "homeServerProviderStatusIcon_" + root.provider
                            Layout.preferredWidth: 15; Layout.preferredHeight: 15
                            Accessible.ignored: true
                            Image {
                                objectName: "homeServerProviderStatusGlyph_" + root.provider
                                anchors.centerIn: parent
                                width: 13; height: 13
                                source: root.providerStatusKind === "linked" ? "../assets/icons/lucide/circle-check.svg"
                                       : root.providerStatusKind === "attention" ? "../assets/icons/lucide/triangle-alert.svg"
                                       : "../assets/icons/link.svg"
                                sourceSize.width: 26
                                fillMode: Image.PreserveAspectFit
                                visible: root.providerStatusKind !== "busy"
                            }
                            Rectangle {
                                visible: root.providerStatusKind === "unlinked"
                                anchors.centerIn: parent
                                width: 15; height: 1.5; radius: 1
                                rotation: -45; color: theme.inkDim
                            }
                            BusyIndicator {
                                visible: root.providerStatusKind === "busy"
                                running: visible
                                anchors.centerIn: parent
                                width: 15; height: 15
                                palette.highlight: theme.inkDim
                                palette.buttonText: theme.inkDim
                                palette.windowText: theme.inkDim
                            }
                        }
                        Label {
                            objectName: "homeServerProviderVisibleStatus"
                            text: root.providerStatus
                            textFormat: Text.PlainText
                            color: root.providerStatusKind === "attention" ? theme.gold
                                   : root.providerStatusKind === "linked" ? theme.vaultHealthy : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(11)
                            elide: Text.ElideRight
                            Layout.maximumWidth: root.width >= 640 ? 180 : 140
                        }
                    }
                }
                Account.AccountButton {
                    id: connectButton
                    objectName: "homeServerConnectButton"
                    Layout.alignment: Qt.AlignRight
                    Layout.minimumWidth: 120
                    implicitWidth: 136
                    text: qsTr("Connect")
                    Accessible.name: root.provider ? qsTr("Connect to %1").arg(root.providerName) : qsTr("Connect a home server")
                    ToolTip.text: Accessible.name
                    ToolTip.delay: 350
                    ToolTip.visible: hovered || activeFocus
                    enabled: !!root.service && root.service.available
                    onClicked: root.panelOpen = true
                }
            }
        }
        Label {
            objectName: "homeServerCardStatus"
            Layout.fillWidth: true
            visible: !root.provider && text.length > 0 && !root.panelOpen
            text: root.providerStatus
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(12)
        }
        Repeater {
            model: root.providerConnections
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
                        Accessible.name: qsTr("Refresh %1").arg(serverRow.modelData.name)
                        ToolTip.text: Accessible.name
                        ToolTip.delay: 350
                        ToolTip.visible: hovered || activeFocus
                        enabled: !!serverRow.modelData.enabled && !serverRow.modelData.needsReconnect && !serverRow.modelData.refreshing
                        onClicked: if (root.service) root.service.refresh(serverRow.modelData.id)
                    }
                    Account.AccountButton {
                        objectName: "homeServerRemove_" + serverRow.modelData.id
                        implicitWidth: 84
                        variant: "link"
                        text: qsTr("Remove")
                        Accessible.name: qsTr("Remove %1 connection").arg(serverRow.modelData.name)
                        ToolTip.text: Accessible.name
                        ToolTip.delay: 350
                        ToolTip.visible: hovered || activeFocus
                        onClicked: if (root.service) root.service.remove(serverRow.modelData.id)
                    }
                }
            }
        }
    }
    Loader {
        objectName: "homeServerPanelLoader"
        active: root.panelOpen
        sourceComponent: HomeServerConnectPanel {
            service: root.service
            initialProvider: root.provider || "jellyfin"
            invoker: connectButton
            onDismissed: root.panelOpen = false
            Component.onCompleted: open()
        }
    }
}
