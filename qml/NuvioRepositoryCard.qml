pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    required property var repository
    required property color accent
    required property bool busy
    required property bool expanded
    signal disclosureRequested()
    signal refreshRequested()
    signal removeRequested()
    signal enableRequested(string pluginId, bool enabled)
    implicitHeight: body.implicitHeight + 20
    radius: 10
    color: "#66141820"
    border.color: theme.edge

    Theme { id: theme }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: card.repository.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%n plugin(s)", "", card.repository.plugins.length)
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 12
                }
            }

            ExtensionsActionButton {
                objectName: "nuvioRepoPlugins_" + card.repository.id
                text: card.expanded ? qsTr("Hide") : qsTr("Plugins")
                onClicked: card.disclosureRequested()
            }
            ExtensionsActionButton {
                text: qsTr("Refresh")
                enabled: !card.busy
                onClicked: card.refreshRequested()
            }
            ExtensionsActionButton {
                text: qsTr("Remove")
                destructive: true
                enabled: !card.busy
                onClicked: card.removeRequested()
            }
        }

        Repeater {
            model: card.expanded ? card.repository.plugins : []
            delegate: RowLayout {
                id: plugin
                required property var modelData
                Layout.fillWidth: true
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: plugin.modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                Text {
                    text: qsTr("Direct HTTP")
                    color: card.accent
                    font.family: theme.ui
                    font.pixelSize: 12
                }
                Switch {
                    id: pluginSwitch
                    enabled: !card.busy
                    checked: plugin.modelData.enabled
                    objectName: "nuvioPluginToggle_" + card.repository.id + "_" + plugin.modelData.id
                    Accessible.name: qsTr("Enable %1").arg(plugin.modelData.name)
                    onToggled: {
                        const requested = checked
                        pluginSwitch.checked = Qt.binding(() => plugin.modelData.enabled)
                        card.enableRequested(plugin.modelData.id, requested)
                    }
                }
            }
        }
    }
}
