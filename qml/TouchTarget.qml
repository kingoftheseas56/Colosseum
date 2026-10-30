import QtQuick
import "TouchMetrics.js" as TouchMetrics

KeyboardAction {
    id: control
    AdaptiveLayout { id:metrics;page:control }
    property bool touch:metrics.touch
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
