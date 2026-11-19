// AbTransportButton — a ±Ns skip control for the audiobook transport bar.
import QtQuick

Item {
    id: root
    property string glyph: "«"
    property string sub: "30"
    signal tapped()
    width: 44; height: 44
    Theme { id: theme }
    Rectangle {
        anchors.fill: parent; radius: 22
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
            anchors.fill: parent
            accessibleName: root.glyph + " " + root.sub
            focusRadius: 22
            onTriggered: root.tapped()
        }
    }
}
