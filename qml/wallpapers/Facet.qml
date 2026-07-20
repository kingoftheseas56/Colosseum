// Facet — a still, QML-drawn geometric wallpaper in the spirit of KDE's "Opal":
// a diagonal gradient under a fine triangular lattice, split by a warm sweeping
// band with a glowing edge. Our own design, pure Qt Quick Shapes, no animation,
// no bitmap. Declares `running` for shelf-contract parity (nothing moves).
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    anchors.fill: parent
    clip: true
    property bool running: true
    property real cell: Math.max(46, width / 28)
    readonly property real diag: Math.sqrt(width * width + height * height)

    // diagonal gradient base
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillGradient: LinearGradient {
                x1: 0; y1: root.height; x2: root.width; y2: 0
                GradientStop { position: 0.0; color: "#2f5fb0" }
                GradientStop { position: 0.48; color: "#7a63c0" }
                GradientStop { position: 0.62; color: "#b98a6a" }
                GradientStop { position: 1.0; color: "#5c3a86" }
            }
            startX: 0; startY: 0
            PathLine { x: root.width; y: 0 }
            PathLine { x: root.width; y: root.height }
            PathLine { x: 0; y: root.height }
        }
    }

    // triangular lattice: verticals + horizontals + a "\" diagonal family = right triangles
    Item {
        anchors.fill: parent
        Repeater {
            model: Math.ceil(root.width / root.cell) + 1
            delegate: Rectangle { required property int index
                x: index * root.cell; width: 1; height: root.height; color: "#ffffff"; opacity: 0.08 }
        }
        Repeater {
            model: Math.ceil(root.height / root.cell) + 1
            delegate: Rectangle { required property int index
                y: index * root.cell; width: root.width; height: 1; color: "#ffffff"; opacity: 0.08 }
        }
        Repeater {
            model: Math.ceil((root.width + root.height) / root.cell) + 2
            delegate: Rectangle {
                required property int index
                property real k: index * root.cell - root.width      // line y = x + k
                width: root.diag * 1.6; height: 1
                x: root.width / 2 - width / 2
                y: root.width / 2 + k - height / 2
                transformOrigin: Item.Center; rotation: 45
                color: "#ffffff"; opacity: 0.06
            }
        }
    }

    // ---- the hero: a warm sweeping band dividing the frame, with a glowing edge ----
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: root.width; y2: root.height
                GradientStop { position: 0.0; color: "#ffbf7a66" }
                GradientStop { position: 1.0; color: "#e88a5a4d" }
            }
            startX: root.width * 0.66; startY: 0
            PathCubic { control1X: root.width*0.60; control1Y: root.height*0.4
                        control2X: root.width*0.46; control2Y: root.height*0.7; x: root.width*0.40; y: root.height }
            PathLine { x: root.width * 0.56; y: root.height }
            PathCubic { control1X: root.width*0.62; control1Y: root.height*0.7
                        control2X: root.width*0.76; control2Y: root.height*0.4; x: root.width*0.82; y: 0 }
            PathLine { x: root.width * 0.66; y: 0 }
        }
    }
    Shape {   // soft wide underglow
        anchors.fill: parent
        opacity: 0.5
        ShapePath {
            fillColor: "transparent"; strokeColor: "#ffb85a"; strokeWidth: 10; capStyle: ShapePath.RoundCap
            startX: root.width * 0.66; startY: 0
            PathCubic { control1X: root.width*0.60; control1Y: root.height*0.4
                        control2X: root.width*0.46; control2Y: root.height*0.7; x: root.width*0.40; y: root.height }
        }
    }
    Shape {   // bright glow edge
        anchors.fill: parent
        ShapePath {
            fillColor: "transparent"; strokeColor: "#ffd27a"; strokeWidth: 2.5; capStyle: ShapePath.RoundCap
            startX: root.width * 0.66; startY: 0
            PathCubic { control1X: root.width*0.60; control1Y: root.height*0.4
                        control2X: root.width*0.46; control2Y: root.height*0.7; x: root.width*0.40; y: root.height }
        }
    }
    Shape {   // faint trailing-edge highlight
        anchors.fill: parent
        opacity: 0.4
        ShapePath {
            fillColor: "transparent"; strokeColor: "#ffcf9a"; strokeWidth: 1.5; capStyle: ShapePath.RoundCap
            startX: root.width * 0.82; startY: 0
            PathCubic { control1X: root.width*0.76; control1Y: root.height*0.4
                        control2X: root.width*0.62; control2Y: root.height*0.7; x: root.width*0.56; y: root.height }
        }
    }
}
