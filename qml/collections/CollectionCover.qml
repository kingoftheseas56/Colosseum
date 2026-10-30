import QtQuick
import ".."

Rectangle {
    id: root
    property url source
    property string title: ""
    property bool preserve: false
    readonly property int status: artwork.status
    color: "#1a1e28"
    radius: 4
    clip: true
    implicitWidth: 160
    implicitHeight: 235
    Accessible.ignored: true
    Text {
        anchors.fill: parent
        anchors.margins: 12
        text: root.title
        textFormat: Text.PlainText
        color: "#c9c8d0"
        font.family: "Segoe UI"
        font.pixelSize: 13
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        visible: artwork.status === Image.Error || artwork.status === Image.Null
    }
    Image {
        id: artwork
        anchors.fill: parent
        source: root.source
        sourceSize.width: Math.max(1, Math.ceil(root.width * 2))
        sourceSize.height: Math.max(1, Math.ceil(root.height * 2))
        fillMode: root.preserve ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.color: "#1affffff"
        border.width: 1
    }
}
