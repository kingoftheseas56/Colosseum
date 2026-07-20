import QtQuick
import QtQuick.Shapes

// A still, QML-drawn mesh-gradient wallpaper: a dark field with several large soft
// colour pools blended into a smooth multi-colour wash. Pure Qt Quick Shapes, no
// animation, no bitmap. `variant` picks a palette+composition.
Item {
    id: root
    anchors.fill: parent
    property bool running: true   // still scene; declared for shelf-contract parity
    property int variant: 0

    readonly property int v: Math.max(0, Math.min(2, variant))
    readonly property var bases: ["#0d1030", "#180a08", "#06130f"]

    function rgbaOf(hex, a) { var c = Qt.color(hex); return Qt.rgba(c.r, c.g, c.b, a); }

    // each blob: {x,y = centre as fraction of w/h; r = radius as fraction of max(w,h);
    // hex = colour; a = alpha at centre}
    readonly property var blobSets: [
        [   // 0 — Twilight (violet / magenta / blue / teal)
            { x:0.22, y:0.28, r:0.55, hex:"#3b2fd6", a:0.85 },
            { x:0.74, y:0.22, r:0.5,  hex:"#d43c9a", a:0.8 },
            { x:0.85, y:0.68, r:0.6,  hex:"#2c6fd6", a:0.8 },
            { x:0.34, y:0.8,  r:0.55, hex:"#7a2ccf", a:0.8 },
            { x:0.55, y:0.5,  r:0.45, hex:"#1fb6c9", a:0.5 },
            { x:0.08, y:0.62, r:0.4,  hex:"#ff5a8c", a:0.4 }
        ],
        [   // 1 — Ember (orange / red / gold / magenta)
            { x:0.24, y:0.3,  r:0.55, hex:"#ff7a2c", a:0.82 },
            { x:0.76, y:0.26, r:0.5,  hex:"#e83a4a", a:0.8 },
            { x:0.82, y:0.72, r:0.58, hex:"#ffb23a", a:0.72 },
            { x:0.32, y:0.82, r:0.55, hex:"#c22a6a", a:0.78 },
            { x:0.54, y:0.52, r:0.42, hex:"#ff9a4a", a:0.5 },
            { x:0.1,  y:0.6,  r:0.4,  hex:"#8a2ccf", a:0.36 }
        ],
        [   // 2 — Aurora Mint (teal / green / cyan / violet)
            { x:0.22, y:0.3,  r:0.55, hex:"#1fd6b0", a:0.8 },
            { x:0.74, y:0.24, r:0.5,  hex:"#3b82f6", a:0.78 },
            { x:0.84, y:0.7,  r:0.58, hex:"#22d3ee", a:0.72 },
            { x:0.34, y:0.82, r:0.55, hex:"#4ade80", a:0.66 },
            { x:0.55, y:0.5,  r:0.44, hex:"#8b5cf6", a:0.5 },
            { x:0.1,  y:0.62, r:0.4,  hex:"#2dd4bf", a:0.4 }
        ]
    ]
    readonly property var blobs: blobSets[v]

    Rectangle { anchors.fill: parent; color: root.bases[root.v] }

    Repeater {
        model: root.blobs
        delegate: Shape {
            required property var modelData
            anchors.fill: parent
            ShapePath {
                strokeWidth: 0
                fillGradient: RadialGradient {
                    centerX: root.width * modelData.x; centerY: root.height * modelData.y
                    centerRadius: Math.max(root.width, root.height) * modelData.r
                    focalX: root.width * modelData.x; focalY: root.height * modelData.y
                    GradientStop { position: 0.0; color: root.rgbaOf(modelData.hex, modelData.a) }
                    GradientStop { position: 0.6; color: root.rgbaOf(modelData.hex, modelData.a * 0.4) }
                    GradientStop { position: 1.0; color: root.rgbaOf(modelData.hex, 0.0) }
                }
                startX: 0; startY: 0
                PathLine { x: root.width; y: 0 }
                PathLine { x: root.width; y: root.height }
                PathLine { x: 0; y: root.height }
            }
        }
    }

    // gentle darkening at the corners so the wash has a centre of gravity
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: root.width * 0.5; centerY: root.height * 0.5
                centerRadius: Math.max(root.width, root.height) * 0.75
                focalX: root.width * 0.5; focalY: root.height * 0.5
                GradientStop { position: 0.55; color: "#00000000" }
                GradientStop { position: 1.0; color: "#40000010" }
            }
            startX: 0; startY: 0
            PathLine { x: root.width; y: 0 }
            PathLine { x: root.width; y: root.height }
            PathLine { x: 0; y: root.height }
        }
    }
}
