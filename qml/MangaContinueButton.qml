pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property string metadata: ""
    property string keyboardObjectName: ""
    signal triggered()
    Theme { id: theme }

    radius: 14
    color: continueChapterMouse.containsMouse ? Qt.lighter(theme.gold, 1.04) : theme.gold
    border.width: 1
    border.color: Qt.rgba(0.13, 0.09, 0.02, 0.3)

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10; anchors.rightMargin: 12
        spacing: 10
        Item {
            width: 36; height: 36
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                anchors.fill: parent
                radius: 11
                color: "#211806"
            }
            Image {
                anchors.centerIn: parent
                width: 24; height: 24
                sourceSize.width: 64; sourceSize.height: 64
                source: Qt.resolvedUrl("../assets/icons/comic-book.svg")
                fillMode: Image.PreserveAspectFit
                smooth: true; cache: true
            }
        }
        Column {
            width: Math.max(0, parent.width - 68)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                width: parent.width
                text: qsTr("Continue reading")
                color: "#211806"; font.family: theme.ui
                font.pixelSize: 14; font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Rectangle {
                width: Math.min(continueMetadataText.implicitWidth + 12, parent.width)
                height: 17; radius: 9
                color: Qt.rgba(0.13, 0.09, 0.02, 0.1)
                border.width: 1
                border.color: Qt.rgba(0.13, 0.09, 0.02, 0.18)
                Text {
                    id: continueMetadataText
                    anchors.fill: parent
                    anchors.leftMargin: 6; anchors.rightMargin: 6
                    verticalAlignment: Text.AlignVCenter
                    text: root.metadata
                    color: "#463417"; font.family: theme.ui
                    font.pixelSize: 10; font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
        }
    }
    MouseArea {
        id: continueChapterMouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: root.triggered()
    }
    KeyboardAction {
        anchors.fill: parent
        pointerEnabled: false
        objectName: root.keyboardObjectName
        focusEnabled: root.visible
        accessibleName: qsTr("Continue reading · %1").arg(root.metadata)
        focusRadius: root.radius
        onTriggered: root.triggered()
    }
}
