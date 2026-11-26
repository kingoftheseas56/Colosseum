import QtQuick

Item {
    id: root
    property bool labelled: false
    property string entryName: "feedbackEntry"
    signal clicked()
    width: labelled ? 112 : 44
    height: 44
    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: 44
        radius: 10
        color: input.interactionActive ? "#18ffffff" : "transparent"
        Image {
            anchors.verticalCenter: parent.verticalCenter
            x: 11
            width: 22; height: 22
            source: "../assets/icons/feedback.svg"
            sourceSize.width: 22; sourceSize.height: 22
            opacity: input.interactionActive ? 1 : 0.8
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: 42
            visible: root.labelled
            text: qsTr("Feedback")
            color: "#f7f7f5"
            font.pixelSize: 13
            font.family: "Segoe UI"
        }
    }
    KeyboardAction {
        id: input
        objectName: root.visible ? root.entryName : ""
        anchors.centerIn: parent
        width: parent.width
        height: 44
        accessibleName: qsTr("Send feedback")
        focusRadius: 10
        onTriggered: root.clicked()
    }
}
