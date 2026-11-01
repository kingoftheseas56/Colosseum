import QtQuick

Item {
    id: root

    property url source
    property string accessibleName: ""
    property int iconSize: 22
    property real idleOpacity: 0.72
    signal triggered()

    width: root.iconSize
    height: root.iconSize

    Image {
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.iconSize
        sourceSize.height: root.iconSize
        fillMode: Image.PreserveAspectFit
        opacity: action.interactionActive ? 1.0 : root.idleOpacity
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    KeyboardAction {
        id: action
        anchors.fill: parent
        accessibleName: root.accessibleName
        focusRadius: 5
        onTriggered: root.triggered()
    }
}
