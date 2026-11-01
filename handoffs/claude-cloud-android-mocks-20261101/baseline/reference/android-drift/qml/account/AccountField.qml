// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root

    property string label: ""
    property string hint: ""
    property string placeholderText: ""
    property bool password: false
    property bool reveal: false
    property alias text: field.text
    property alias inputMethodHints: field.inputMethodHints
    property alias maximumLength: field.maximumLength
    property string controlObjectName: ""

    signal accepted()

    implicitHeight: labelText.implicitHeight
        + (root.hint.length > 0 ? hintText.implicitHeight + 4 : 0)
        + 10
        + field.height
    implicitWidth: 400

    Theme { id: theme }
    AdaptiveLayout { id:metrics; page:root }

    Text {
        id: labelText
        anchors.left: parent.left
        anchors.top: parent.top
        text: root.label
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: theme.fontSize(12)
        font.weight: Font.DemiBold
    }

    Text {
        id: hintText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: labelText.bottom
        anchors.topMargin: 4
        visible: root.hint.length > 0
        text: root.hint
        color: theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: theme.fontSize(11)
        wrapMode: Text.WordWrap
    }

    TouchTextField {
        id: field
        objectName: root.controlObjectName
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.hint.length > 0 ? hintText.bottom : labelText.bottom
        anchors.topMargin: 10
        height:metrics.touch ? metrics.touchTargetSize : 46
        leftPadding: 14
        rightPadding: root.password ? (metrics.touch ? 60 : 48) : 14
        color: theme.ink
        placeholderTextColor: theme.inkDimmer
        placeholderText: root.placeholderText
        font.family: theme.ui
        font.pixelSize: theme.fontSize(13)
        echoMode: root.password && !root.reveal
            ? TextInput.Password
            : TextInput.Normal
        selectByMouse: true

        background: Rectangle {
            radius: 12
            color: Qt.rgba(0.015, 0.018, 0.03, 0.68)
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? theme.gold : theme.edge
        }

        onAccepted: root.accepted()
    }

    TouchButton {
        id: revealButton
        objectName: root.controlObjectName.length > 0
            ? root.controlObjectName + "Reveal"
            : ""
        visible: root.password
        anchors.right: field.right
        anchors.rightMargin:metrics.touch ? 0 : 8
        anchors.verticalCenter: field.verticalCenter
        width:metrics.touch ? metrics.touchTargetSize : 32
        height:width
        flat: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: root.reveal ? "Hide password" : "Show password"

        contentItem: Text {
            text: root.reveal ? "○" : "◉"
            color: revealButton.hovered ? theme.ink : theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: theme.fontSize(15)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 7
            color: revealButton.hovered
                ? Qt.rgba(1, 1, 1, 0.07)
                : "transparent"
        }

        onClicked: root.reveal = !root.reveal
    }

    function clear() {
        field.text = ""
        root.reveal = false
    }

    function forceInputFocus() {
        field.forceActiveFocus()
    }
}
