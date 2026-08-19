// AccountDeviceIcon.qml
// Native monitor glyph transcribed from the locked Account Centre Devices mock.

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property color strokeColor: "#c9c8d0"
    property real strokeWidth: 1.5
    property real glyphSize: 20

    implicitWidth: glyphSize
    implicitHeight: glyphSize

    readonly property real scaleFactor: glyphSize / 24.0

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.scaleFactor
        antialiasing: true

        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M5 5h14a1.5 1.5 0 0 1 1.5 1.5V15a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 15V6.5A1.5 1.5 0 0 1 5 5Z" }
        }

        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M8.5 20h7M12 16.5V20" }
        }
    }
}
