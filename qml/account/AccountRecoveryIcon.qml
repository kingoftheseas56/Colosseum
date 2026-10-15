// AccountRecoveryIcon.qml
// Native vector glyphs transcribed from the locked Account Centre Recovery mock.

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property string kind: "key"
    property color strokeColor: "#f0c44a"
    property real strokeWidth: 1.5
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
            strokeColor: root.kind === "key" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M12.75 11a4.25 4.25 0 1 1-8.5 0 4.25 4.25 0 0 1 8.5 0Z" }
        }

        ShapePath {
            strokeColor: root.kind === "key" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M12.6 11H21M17.2 11v3M14.6 11v2" }
        }

        ShapePath {
            strokeColor: root.kind === "replace" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M8 8a5 5 0 1 1-1 7" }
        }

        ShapePath {
            strokeColor: root.kind === "replace" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M4 16v-5h5" }
        }

        ShapePath {
            strokeColor: root.kind === "recovery" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M12 3.5 19 7v5c0 4.2-2.4 7-7 8.5C7.4 19 5 16.2 5 12V7l7-3.5Z" }
        }

        ShapePath {
            strokeColor: root.kind === "recovery" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M9 12h6" }
        }
    }
}
