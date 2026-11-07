import QtQuick

Rectangle {
    id: card
    required property var entry
    property string instanceLabel: ""
    property string capabilities: ""
    property bool management: false
    property bool compact: false
    property bool configurable: false
    signal manageRequested()
    signal toggleRequested()
    signal configureRequested()
    signal removeRequested()
    signal keyboardFocusReceived()
    signal tabForwardRequested()
    readonly property var manifest: entry.manifest || ({})
    readonly property string addonName: String(manifest.name || qsTr("Unnamed add-on"))
    readonly property bool core: entry.core === true
    readonly property bool isEnabled: entry.enabled === true
    readonly property int inset: compact ? 12 : 16
    readonly property int logoSize: compact ? 36 : 48
    function focusFirstAction() {
        if (!card.management) return false
        if (toggleAction.enabled) { toggleAction.forceActiveFocus(Qt.TabFocusReason); return true }
        if (configureAction.visible && configureAction.enabled) {
            configureAction.forceActiveFocus(Qt.TabFocusReason)
            return true
        }
        if (removeAction.enabled) { removeAction.forceActiveFocus(Qt.TabFocusReason); return true }
        return false
    }
    Theme { id: theme }
    implicitHeight: body.height + 2 * card.inset
    radius: card.compact ? 12 : 16
    color: "#cc141820"
    border.width: 1
    border.color: openAction.interactionActive && !management ? theme.gold : theme.edge

    Column {
        id: body
        x: card.inset; y: card.inset
        width: parent.width - 2 * card.inset
        spacing: card.compact ? 9 : 14
        Row {
            width: parent.width
            spacing: card.compact ? 10 : 14
            AddonLogo {
                size: card.logoSize
                addonId: String(card.entry.id || "")
                addonName: card.addonName
                manifestLogo: String(card.manifest.logo || "")
                preferManifestLogo: true
            }
            Column {
                width: parent.width - card.logoSize - parent.spacing
                spacing: 5
                Text {
                    width: parent.width
                    text: card.addonName
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: theme.ink
                    font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: (card.core ? qsTr("Core · Always on") : card.isEnabled ? qsTr("Enabled") : qsTr("Disabled"))
                          + (card.instanceLabel.length ? " · " + card.instanceLabel : "")
                    elide: Text.ElideRight
                    color: card.isEnabled ? theme.gold : theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12
                }
            }
        }
        Text {
            width: parent.width
            text: card.capabilities
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 12
        }
        Flow {
            visible: card.management
            width: parent.width
            spacing: 8
            ExtensionsActionButton {
                id: toggleAction
                text: card.isEnabled ? qsTr("Disable") : qsTr("Enable")
                enabled: !card.core
                onClicked: card.toggleRequested()
                onKeyboardFocusReceived: card.keyboardFocusReceived()
                onTabNavigationRequested: card.tabForwardRequested()
            }
            ExtensionsActionButton {
                id: configureAction
                visible: card.configurable
                text: qsTr("Configure")
                tabNavigationExit: card.core
                onClicked: card.configureRequested()
                onKeyboardFocusReceived: card.keyboardFocusReceived()
                onTabNavigationRequested: card.tabForwardRequested()
            }
            ExtensionsActionButton {
                id: removeAction
                text: qsTr("Remove")
                destructive: true
                enabled: !card.core
                tabNavigationExit: !card.core
                onClicked: card.removeRequested()
                onKeyboardFocusReceived: card.keyboardFocusReceived()
                onTabNavigationRequested: card.tabForwardRequested()
            }
        }
    }
    KeyboardAction {
        id: openAction
        anchors.fill: parent
        visible: !card.management
        accessibleName: qsTr("Manage %1").arg(card.addonName)
        focusRadius: card.radius
        onTriggered: card.manageRequested()
        onActiveFocusChanged: if (activeFocus) card.keyboardFocusReceived()
    }
}
