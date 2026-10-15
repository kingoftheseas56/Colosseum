// AccountSecurityIcon.qml
// Native vector glyphs transcribed from the locked Account Centre Security mock.

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property string kind: "shield"
    property color strokeColor: "#f0c44a"
    property real strokeWidth: 1.6
    property real glyphSize: 24

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
            strokeColor: root.kind === "shield" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M12 3.5 19 7v5c0 4.2-2.4 7-7 8.5C7.4 19 5 16.2 5 12V7l7-3.5Z" }
        }

        ShapePath {
            strokeColor: root.kind === "shield" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "m9.2 12.3 1.8 1.8 3.9-4.1" }
        }

        ShapePath {
            strokeColor: root.kind === "lock" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M7 10h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z" }
        }

        ShapePath {
            strokeColor: root.kind === "lock" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M8.5 10V7.5a3.5 3.5 0 0 1 7 0V10" }
        }

        ShapePath {
            strokeColor: root.kind === "logout" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M9 5H5v14h4" }
        }

        ShapePath {
            strokeColor: root.kind === "logout" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "m14 8 4 4-4 4M18 12H9" }
        }
    }
}
