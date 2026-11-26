import QtQuick

Rectangle {
    id: placeholder
    objectName: "biblioCoverPlaceholder"
    property string title: ""

    Theme { id: theme }

    gradient: Gradient {
        GradientStop { position: 0; color: "#33405c" }
        GradientStop { position: 1; color: "#12161f" }
    }
    Text {
        anchors.centerIn: parent
        width: parent.width - Math.min(24, parent.width / 5)
        text: placeholder.title || qsTr("Untitled")
        color: Qt.rgba(1, 1, 1, 0.8)
        font.family: theme.display
        font.pixelSize: Math.min(17, Math.max(10, Math.floor(parent.width / 7)))
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 4
        elide: Text.ElideRight
    }
}
