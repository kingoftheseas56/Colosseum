import QtQuick

// Bounded artwork only. URL resolution and candidate selection stay with the owner.
Item {
    id: root
    property alias source: picture.source
    property bool crop: false
    property bool showPlaceholder: true
    property color backgroundColor: "#202326"
    readonly property bool ready: picture.status === Image.Ready
    readonly property int imageStatus: picture.status
    readonly property real decodeScale: Math.max(1, Math.min(2, Screen.devicePixelRatio))
    implicitWidth: 196
    implicitHeight: width
    clip: true

    Rectangle { anchors.fill: parent; color: root.backgroundColor }
    Item {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.22
        height: width
        visible: !root.ready && root.showPlaceholder
        opacity: 0.35
        Rectangle {
            x: parent.width * 0.1; y: parent.height * 0.61
            width: parent.width * 0.35; height: parent.height * 0.25
            radius: height / 2; rotation: -15; color: "#c9c8d0"
        }
        Rectangle {
            x: parent.width * 0.39; y: parent.height * 0.16
            width: Math.max(1, parent.width * 0.06); height: parent.height * 0.59
            color: "#c9c8d0"
        }
        Rectangle {
            x: parent.width * 0.39; y: parent.height * 0.16
            width: parent.width * 0.38; height: Math.max(2, parent.height * 0.12)
            rotation: -10; transformOrigin: Item.Left; color: "#c9c8d0"
        }
    }
    Image {
        id: picture
        objectName: "vinylArtworkImage"
        anchors.fill: parent
        fillMode: root.crop ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        asynchronous: true
        cache: true
        smooth: true
        mipmap: true
        sourceSize.width: Math.max(1, Math.ceil(root.width * root.decodeScale))
        sourceSize.height: Math.max(1, Math.ceil(root.height * root.decodeScale))
        visible: status === Image.Ready
    }
}
