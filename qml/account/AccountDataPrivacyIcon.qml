// AccountDataPrivacyIcon.qml
// Native vector glyphs transcribed from the locked Data & privacy HTML.

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property string kind: "search"
    property color strokeColor: "#c9c8d0"
    property real strokeWidth: 1.5
    property real glyphSize: 18

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
            strokeColor: root.kind === "search" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M16 10.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0Z" }
        }
        ShapePath {
            strokeColor: root.kind === "search" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M15 15l4 4" }
        }

        ShapePath {
            strokeColor: root.kind === "activity" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M5 18V9M10 18V5M15 18v-7M20 18V7" }
        }

        ShapePath {
            strokeColor: root.kind === "sync" || root.kind === "portable"
                ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M7 17.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.2 8.8 4.4 4.4 0 0 0 7 17.5Z" }
        }
        ShapePath {
            strokeColor: root.kind === "sync" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M9 12h6" }
        }

        ShapePath {
            strokeColor: root.kind === "export" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M12 4v11M8 11l4 4 4-4M5 19h14" }
        }

        ShapePath {
            strokeColor: root.kind === "device" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M5 5h14a1.5 1.5 0 0 1 1.5 1.5V15A1.5 1.5 0 0 1 19 16.5H5A1.5 1.5 0 0 1 3.5 15V6.5A1.5 1.5 0 0 1 5 5Z" }
        }
        ShapePath {
            strokeColor: root.kind === "device" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M8.5 20h7M12 16.5V20" }
        }

        ShapePath {
            strokeColor: root.kind === "secret" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M7 10h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2Z" }
        }
        ShapePath {
            strokeColor: root.kind === "secret" ? root.strokeColor : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M8.5 10V7.5a3.5 3.5 0 0 1 7 0V10" }
        }
    }
}
