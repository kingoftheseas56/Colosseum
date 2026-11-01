import QtQuick

TouchTarget {
    id: root

    property url source
    property int iconSize: 22
    property real idleOpacity: 0.72

    visualWidth:root.iconSize
    visualHeight:root.iconSize

    Image {
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.iconSize
        sourceSize.height: root.iconSize
        fillMode: Image.PreserveAspectFit
        opacity: root.interactionActive ? 1.0 : root.idleOpacity
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    focusRadius:5
}
