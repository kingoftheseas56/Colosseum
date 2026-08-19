// AccountAvatarGlyph.qml
// Production vector asset for the locked Account Centre Profile medallions.
// Geometry is transcribed from requests/colosseum-account-centre-profile-qml/preview.html.

import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property string avatarId: "initial"
    property string initial: "?"
    property color strokeColor: "#c9c8d0"
    property real strokeWidth: 1.6
    property real glyphSize: Math.min(width, height)

    implicitWidth: 32
    implicitHeight: 32

    Text {
        anchors.centerIn: parent
        visible: root.avatarId === "initial"
        text: root.initial
        color: root.strokeColor
        font.pixelSize: Math.max(10, root.glyphSize * 0.66)
        font.weight: Font.DemiBold
    }

    Item {
        id: vectorViewport
        width: 32
        height: 32
        anchors.centerIn: parent
        visible: root.avatarId !== "initial"
        scale: Math.min(root.width / 32, root.height / 32)

        Shape {
            anchors.fill: parent
            visible: root.avatarId === "laurel"

            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M15 25C9.7 22.3 7.5 17.7 7.5 12.2M17 25c5.3-2.7 7.5-7.3 7.5-12.8" }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M7.8 14c-2.4-.7-3.5-2.1-3.5-4.3 2.6.1 4.1 1.2 4.6 3.5M8.8 18.3c-2.6-.1-4-1.2-4.5-3.3 2.5-.5 4.3.3 5.2 2.5M11.1 22c-2.4.5-4.1-.2-5.1-2.1 2.2-1.1 4-.8 5.5.9" }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M24.2 14c2.4-.7 3.5-2.1 3.5-4.3-2.6.1-4.1 1.2-4.6 3.5M23.2 18.3c2.6-.1 4-1.2 4.5-3.3-2.5-.5-4.3.3-5.2 2.5M20.9 22c2.4.5 4.1-.2 5.1-2.1-2.2-1.1-4-.8-5.5.9" }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.avatarId === "column"
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M8 8h16M10 5.5h12M10 26.5h12M8 24h16M11.5 8v16M20.5 8v16M14.5 8v16M17.5 8v16" }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.avatarId === "book"
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M5.5 8.5c4.1-.5 7.6.4 10.5 2.7v14c-2.9-2.3-6.4-3.2-10.5-2.7v-14ZM26.5 8.5c-4.1-.5-7.6.4-10.5 2.7v14c2.9-2.3 6.4-3.2 10.5-2.7v-14Z" }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.avatarId === "screen"
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M7.5 7h17a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2h-17a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2Z" }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "m14 11.7 6 2.8-6 2.8v-5.6ZM11.5 26h9" }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.avatarId === "panels"
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M7 6h6a1 1 0 0 1 1 1v7a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1ZM18 6h7a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1ZM18 14h7a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1V15a1 1 0 0 1 1-1ZM7 18h6a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1v-6a1 1 0 0 1 1-1Z" }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.avatarId === "custom"
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.strokeColor
                strokeWidth: root.strokeWidth
                capStyle: ShapePath.RoundCap
                PathSvg { path: "M16 8v16M8 16h16" }
            }
        }
    }
}
