import QtQuick
import QtQuick.Controls.Basic

Item {
    id: control
    required property var sourcePicker
    readonly property bool checked: sourcePicker.smartPlayQuality !== "off"
    readonly property string explanationText: qsTr("Smart Pick starts a streaming source automatically at your preferred quality. Turn it off to choose a source yourself.")
    implicitWidth: 252
    implicitHeight: 44
    width: implicitWidth
    height: implicitHeight

    Theme { id: theme }

    Rectangle {
        width: parent.width - 44
        height: 44
        radius: 11
        color: toggleAction.interactionActive ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
        border.width: 1
        border.color: control.checked ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
        Row {
            anchors.centerIn: parent
            spacing: 8
            Text {
                text: qsTr("Smart Pick")
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 12
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: !control.checked ? qsTr("Off") : control.sourcePicker.smartPlayQuality === "4k" ? qsTr("4K") : qsTr("1080p")
                color: control.checked ? theme.gold : theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                width: 30; height: 18; radius: 9
                color: control.checked ? theme.gold : Qt.rgba(1, 1, 1, 0.18)
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    x: control.checked ? 14 : 3; y: 3
                    width: 12; height: 12; radius: 6
                    color: control.checked ? "#1a1306" : theme.inkDim
                }
            }
        }
        KeyboardAction {
            id: toggleAction
            objectName: "smartPickSwitch"
            anchors.fill: parent
            spaceActivates: true
            accessibleName: qsTr("Smart Pick")
            accessibleDescription: control.explanationText
            Accessible.role: Accessible.CheckBox
            Accessible.checkable: true
            Accessible.checked: control.checked
            Accessible.onToggleAction: activate(Qt.OtherFocusReason)
            onTriggered: control.sourcePicker.setSmartPickEnabled(!control.checked)
        }
    }
    KeyboardAction {
        id: infoAction
        objectName: "smartPickInfo"
        anchors.right: parent.right
        width: 44; height: 44
        spaceActivates: true
        accessibleName: qsTr("About Smart Pick")
        accessibleDescription: control.explanationText
        Keys.onEscapePressed: explanation.close()
        onTriggered: {
            explanation.open()
            explanationTimer.restart()
        }
        Text {
            anchors.centerIn: parent
            text: qsTr("ⓘ")
            font.family: theme.ui
            font.pixelSize: 19
            color: infoAction.interactionActive ? theme.gold : theme.inkDim
        }
    }
    Timer {
        id: explanationTimer
        interval: 12000
        onTriggered: explanation.close()
    }
    ToolTip {
        id: hoverExplanation
        objectName: "smartPickHoverExplanation"
        parent: Overlay.overlay
        visible: control.visible && infoAction.hovered && !explanation.visible
        delay: 350
        text: control.explanationText
        width: Math.min(340, Math.max(0, parent ? parent.width - 24 : 340))
        x: Math.max(12, Math.min(parent ? parent.width - width - 12 : 12,
                               infoAction.mapToItem(parent, 0, 0).x + 44 - width))
        y: Math.max(12, Math.min(parent ? parent.height - height - 12 : 12,
                               infoAction.mapToItem(parent, 0, 44).y + 8))
        padding: 14
        background: Rectangle {
            radius: 11
            color: "#171a22"
            border.width: 1
            border.color: theme.edge
        }
        contentItem: Text {
            text: hoverExplanation.text
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    Popup {
        id: explanation
        objectName: "smartPickExplanation"
        parent: Overlay.overlay
        width: Math.min(340, Math.max(0, parent ? parent.width - 24 : 340))
        x: Math.max(12, Math.min(parent ? parent.width - width - 12 : 12,
                               infoAction.mapToItem(parent, 0, 0).x + 44 - width))
        y: Math.max(12, Math.min(parent ? parent.height - height - 12 : 12,
                               infoAction.mapToItem(parent, 0, 44).y + 8))
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: explanationTimer.stop()
        background: Rectangle {
            radius: 11
            color: "#171a22"
            border.width: 1
            border.color: theme.edge
        }
        contentItem: Text {
            text: control.explanationText
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
}
