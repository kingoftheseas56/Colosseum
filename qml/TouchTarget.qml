import QtQuick
import "TouchMetrics.js" as TouchMetrics

KeyboardAction {
    id: control
    property bool touch: Qt.platform.os === "android"
        || !!(Window.window && Window.window.androidHost)
    property real visualWidth: 24
    property real visualHeight: 24
    readonly property real minimumTargetSize: touch ? TouchMetrics.minimumTargetSize : 0
    default property alias content: visual.data
    implicitWidth: Math.max(minimumTargetSize, visualWidth)
    implicitHeight: Math.max(minimumTargetSize, visualHeight)
    Item {
        id: visual
        anchors.centerIn: parent
        width: control.visualWidth
        height: control.visualHeight
    }
}
