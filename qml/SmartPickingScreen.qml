import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: screen
    property bool allowManual: true
    signal manualRequested()
    color: "#0b0d12"
    Theme { id: theme }

    Column {
        id: content
        width: Math.max(0, Math.min(560, screen.width - 48))
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(76, (screen.height - height) / 2)
        spacing: 20
        Text {
            width: parent.width
            text: qsTr("SMART PICK")
            color: theme.gold
            font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 3
            horizontalAlignment: Text.AlignHCenter
        }
        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48; height: 48
            running: screen.visible
            palette.highlight: theme.gold
        }
        Text {
            objectName: "smartPickingHeading"
            width: parent.width
            text: qsTr("Picking the best stream…")
            color: theme.ink
            font.family: theme.display; font.pixelSize: screen.width < 480 ? 27 : 36
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            horizontalAlignment: Text.AlignHCenter
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Text {
            width: parent.width
            text: qsTr("Comparing available sources")
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            horizontalAlignment: Text.AlignHCenter
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, manualLabel.implicitWidth + 36)
            height: Math.max(48, manualLabel.implicitHeight + 24)
            radius: 11
            color: manualAction.interactionActive ? theme.glassHi : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1; border.color: theme.edge
            enabled: screen.allowManual
            opacity: enabled ? 1 : 0.5
            Text {
                id: manualLabel
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 24)
                text: qsTr("Choose source manually")
                color: theme.ink
                font.family: theme.ui; font.pixelSize: 14
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                horizontalAlignment: Text.AlignHCenter
            }
            KeyboardAction {
                id: manualAction
                objectName: "smartPickingManualAction"
                anchors.fill: parent
                accessibleName: manualLabel.text
                spaceActivates: true
                onTriggered: screen.manualRequested()
            }
        }
    }
}
