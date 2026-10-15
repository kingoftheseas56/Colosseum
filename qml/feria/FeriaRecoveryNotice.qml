import QtQuick
import QtQuick.Layouts
import ".." as Colosseum

Column {
    id: root
    property string message: ""
    property string actionText: ""
    property bool actionEnabled: true
    property string secondaryText: ""
    property string automationId: "feriaRecovery"
    signal focusEntered()
    signal actionRequested()
    signal secondaryRequested()
    visible: message.length > 0
    spacing: 10
    function focusAction() { if (primary.visible && primary.enabled) primary.forceActiveFocus(Qt.TabFocusReason) }
    Colosseum.Theme { id: theme }
    Text {
        width: parent.width
        text: root.message
        visible: text.length > 0
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 14
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StatusBar
    }
    Flow {
        width: parent.width; spacing: 16
        visible: root.actionText.length > 0 || root.secondaryText.length > 0
        Text {
            text: root.actionText
            visible: text.length > 0
            color: root.actionEnabled ? theme.gold : theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 14; font.underline: true
            height: 32; verticalAlignment: Text.AlignVCenter
            Colosseum.KeyboardAction {
                id: primary
                anchors.fill: parent
                objectName: root.automationId + "Action"
                accessibleName: root.actionText
                enabled: root.actionEnabled
                spaceActivates: true
                onActiveFocusChanged: if (activeFocus) root.focusEntered()
                onTriggered: root.actionRequested()
            }
        }
        Text {
            text: root.secondaryText
            visible: text.length > 0
            color: theme.gold
            font.family: theme.ui; font.pixelSize: 14; font.underline: true
            height: 32; verticalAlignment: Text.AlignVCenter
            Colosseum.KeyboardAction {
                anchors.fill: parent
                objectName: root.automationId + "Secondary"
                accessibleName: root.secondaryText
                spaceActivates: true
                onActiveFocusChanged: if (activeFocus) root.focusEntered()
                onTriggered: root.secondaryRequested()
            }
        }
    }
}
