// AbTransportButton — a ±Ns skip control for the audiobook transport bar.
import QtQuick

Item {
    id: root
    property string glyph: "«"
    property string sub: "30"
    signal tapped()
    width: Qt.platform.os === "android" ? 124 : 44
    height: Qt.platform.os === "android" ? 70 : 44
    Theme { id: theme }
    Rectangle {
        width: 44; height: 44; radius: 22
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        color: input.interactionActive ? Qt.rgba(1,1,1,0.10) : "transparent"
        Column {
            anchors.centerIn: parent; spacing: -2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.glyph
                color: theme.ink; font.pixelSize: 18 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sub
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 9 }
        }
        KeyboardAction {
            id: input
            width: root.width; height: Qt.platform.os === "android" ? 48 : 44
            x: (parent.width - width) / 2; y: 0
            accessibleName: root.glyph + " " + root.sub
            tooltipText: (root.glyph === "«" ? qsTr("Skip back ") : qsTr("Skip forward ")) + root.sub + qsTr(" seconds")
            androidTooltipPolicy: "caption"
            androidCaptionText: (root.glyph === "«" ? qsTr("Back ") : qsTr("Forward ")) + root.sub + qsTr(" seconds")
            focusRadius: 22
            onTriggered: root.tapped()
        }
    }
}
