pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: bar
    required property var store
    signal focusRequested(Item item)
    readonly property color accent: "#a3c3ff"
    property string expandedRepo: ""
    property int countBeforeAdd: -1
    width: parent ? parent.width : 800
    implicitHeight: body.implicitHeight + 32
    height: implicitHeight
    radius: 15
    color: "#88202a42"
    border.color: "#656e97d0"

    function offerLink(link: string) {
        address.text = link
        address.forceActiveFocus()
        address.selectAll()
        bar.focusRequested(address)
    }

    function submit() {
        if (!addButton.enabled)
            return
        bar.countBeforeAdd = bar.store.repositories.length
        bar.store.addRepository(address.text)
    }

    Theme { id: theme }

    Connections {
        target: bar.store
        function onChanged() {
            if (bar.countBeforeAdd >= 0 && bar.store.repositories.length > bar.countBeforeAdd)
                address.clear()
        }
        function onBusyChanged() {
            if (!bar.store.busy)
                bar.countBeforeAdd = -1
        }
        function onErrorChanged() {
            if (bar.store.error.length)
                bar.countBeforeAdd = -1
        }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: qsTr("Nuvio Plugins")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 23
                font.weight: Font.DemiBold
            }
            Rectangle {
                implicitWidth: brand.implicitWidth + 18
                implicitHeight: brand.implicitHeight + 8
                color: "#333f5f"
                border.color: bar.accent
                radius: 8

                Text {
                    id: brand
                    anchors.centerIn: parent
                    text: qsTr("Nuvio")
                    color: bar.accent
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Add a repository link you trust, then turn on the plugins you want to run for Theatre sources. Repository settings are stored on this device.")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: address
                objectName: "nuvioRepositoryAddress"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                maximumLength: 2048
                enabled: bar.store !== null && !bar.store.busy
                placeholderText: qsTr("HTTPS repository link")
                Accessible.name: qsTr("Nuvio repository link")
                color: theme.ink
                placeholderTextColor: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                leftPadding: 12
                rightPadding: 12
                selectByMouse: true
                background: Rectangle {
                    radius: 9
                    color: "#66141820"
                    border.color: address.activeFocus ? bar.accent : theme.edge
                }
                onAccepted: bar.submit()
            }

            ExtensionsActionButton {
                id: addButton
                objectName: "addNuvioRepository"
                text: qsTr("Add Nuvio repo")
                enabled: address.enabled && address.text.trim().length > 0
                onClicked: bar.submit()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: bar.store && bar.store.busy ? qsTr("Updating Nuvio repositories…")
                : bar.store ? bar.store.error : ""
            color: bar.store && bar.store.busy ? theme.inkDim : "#ffb4ab"
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }

        Repeater {
            model: bar.store ? bar.store.repositories : []
            delegate: NuvioRepositoryCard {
                required property var modelData
                Layout.fillWidth: true
                repository: modelData
                accent: bar.accent
                busy: bar.store.busy
                expanded: bar.expandedRepo === repository.id
                onDisclosureRequested: bar.expandedRepo = expanded ? "" : repository.id
                onRefreshRequested: bar.store.refreshRepository(repository.id)
                onRemoveRequested: {
                    bar.store.removeRepository(repository.id)
                    if (!bar.store.error.length && bar.expandedRepo === repository.id)
                        bar.expandedRepo = ""
                }
                onEnableRequested: (pluginId, enabled) => bar.store.setPluginEnabled(repository.id, pluginId, enabled)
            }
        }
    }
}
