import QtQuick
import ".."

Rectangle {
    id: offer
    property string label: ""
    signal accepted()
    signal dismissed()

    implicitWidth: Math.min(540, body.implicitWidth + dismiss.width + 22)
    implicitHeight: Math.max(42, body.implicitHeight + 16)
    visible: label.length > 0
    radius: 10
    color: "#202128"
    border.color: theme.edge

    Theme { id: theme }
    KeyboardAction {
        anchors.fill: parent
        anchors.rightMargin: dismiss.width + 4
        accessibleName: offer.label
        onTriggered: offer.accepted()
        Text {
            id: body
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 4
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: theme.fontSize(13)
            text: offer.label
        }
    }
    KeyboardAction {
        id: dismiss
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 40
        height: parent.height
        accessibleName: qsTr("Dismiss jump offer")
        onTriggered: offer.dismissed()
        Text {
            anchors.centerIn: parent
            color: theme.inkDim
            font.pixelSize: 20
            text: "×"
        }
    }
}
