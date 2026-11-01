import QtQuick 6

FocusScope {
    id: root
    objectName: "playerLoadFailure"
    signal retryRequested()
    signal backRequested()
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onEscapePressed: root.backRequested()

    Rectangle { anchors.fill: parent; color: "#101218" }
    Column {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 48)
        spacing: 18
        Text {
            width: parent.width
            text: "Couldn't open the video player"
            color: "white"
            font.pixelSize: 24
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: "Try again, or go back to choose another video. If this continues, restart Colosseum."
            color: "#bec5d2"
            font.pixelSize: 15
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            Rectangle {
                width: 144; height: 44; radius: 22; color: "#f2cd55"
                Text { anchors.centerIn: parent; text: "Try again"; color: "#101218"; font.pixelSize: 15 }
                KeyboardAction {
                    objectName: "playerLoadRetry"
                    anchors.fill: parent
                    accessibleName: "Try opening the video player again"
                    focus: true
                    onTriggered: root.retryRequested()
                }
            }
            Rectangle {
                width: 112; height: 44; radius: 22; color: "#303642"
                Text { anchors.centerIn: parent; text: "Back"; color: "white"; font.pixelSize: 15 }
                KeyboardAction {
                    objectName: "playerLoadBack"
                    anchors.fill: parent
                    accessibleName: "Go back"
                    onTriggered: root.backRequested()
                }
            }
        }
    }
}
