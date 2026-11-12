import QtQuick
import QtQuick.Shapes

// Presentation only. The containing VinylItemCard owns its title and all input.
Item {
    id: root
    property var artworkSources: []
    property string title: ""
    implicitWidth: 196
    implicitHeight: width * 194 / 304

    readonly property real objectScale: Math.max(0, Math.min(width / 304, height / 194))

    Item {
        id: cassette
        width: 300
        height: 190
        x: (root.width - width * scale) / 2
        y: (root.height - height * scale) / 2
        scale: root.objectScale
        transformOrigin: Item.TopLeft

        Rectangle {
            x: 3; y: 5; width: 294; height: 183; radius: 14
            color: "#10000000"
            border.color: "#161719"
            border.width: 1
        }
        Rectangle {
            x: 0; y: 0; width: 300; height: 184; radius: 14
            gradient: Gradient {
                GradientStop { position: 0; color: "#666964" }
                GradientStop { position: 0.025; color: "#383b38" }
                GradientStop { position: 0.55; color: "#252926" }
                GradientStop { position: 1; color: "#171b19" }
            }
            border.color: "#85877d"
            border.width: 1
        }
        Rectangle {
            x: 4; y: 4; width: 292; height: 176; radius: 11
            color: "transparent"
            border.color: "#383f38"
            border.width: 1
        }

        // An actual paper label seats the mix's song covers above the tape window.
        Rectangle {
            x: 15; y: 13; width: 270; height: 137; radius: 8
            gradient: Gradient {
                GradientStop { position: 0; color: "#e1ddd0" }
                GradientStop { position: 1; color: "#c8c5b7" }
            }
            border.color: "#b0ad9e"
            border.width: 1
        }
        Rectangle {
            x: 23; y: 19; width: 254; height: 127; radius: 4
            color: "transparent"
            border.color: "#a5a396"
            border.width: 1
        }
        Row {
            x: 39
            y: 23
            spacing: 6
            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    width: 70
                    height: 70
                    color: "#aba99e"
                    VinylArtwork {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: root.artworkSources && index < root.artworkSources.length
                            ? root.artworkSources[index] || "" : ""
                        crop: false
                        backgroundColor: "#aaa89e"
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#40000000"
                        border.width: 1
                    }
                }
            }
        }
        Rectangle { x: 25; y: 97; width: 250; height: 1; color: "#9b9d92" }

        // One continuous inset window and two separate, stationary reel hubs.
        Rectangle {
            x: 33; y: 103; width: 234; height: 43; radius: 21.5
            color: "#111615"
            border.color: "#777d72"
            border.width: 2
        }
        Rectangle {
            x: 101; y: 109; width: 98; height: 30; radius: 3
            gradient: Gradient {
                GradientStop { position: 0; color: "#302c25" }
                GradientStop { position: 0.45; color: "#4e4334" }
                GradientStop { position: 1; color: "#262620" }
            }
            border.color: "#0b100e"
            border.width: 1
        }
        Rectangle { x: 117; y: 115; width: 66; height: 1; color: "#786e59"; opacity: 0.42 }
        Rectangle { x: 117; y: 131; width: 66; height: 1; color: "#090e0b" }
        Repeater {
            model: 2
            delegate: Item {
                required property int index
                x: index === 0 ? 55 : 203
                y: 105
                width: 42
                height: 39
                Rectangle {
                    anchors.centerIn: parent
                    width: 38; height: 38; radius: 19
                    color: "#4d4c41"
                    border.color: "#989a89"
                    border.width: 1
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 30; height: 30; radius: 15
                    color: "#c3c2ae"
                    border.color: "#dedccd"
                    border.width: 1
                    Repeater {
                        model: 6
                        delegate: Rectangle {
                            required property int index
                            x: 13; y: 3; width: 4; height: 7; radius: 1
                            color: "#4c5147"
                            transform: Rotation { origin.x: 2; origin.y: 12; angle: index * 60 }
                        }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 9; height: 9; radius: 4.5
                        color: "#151d18"
                        border.color: "#9a9e8c"
                        border.width: 1
                    }
                }
            }
        }
        Rectangle { x: 56; y: 105; width: 187; height: 1; color: "#80958e"; opacity: 0.42 }

        // The tapered tape-mouth housing is part of the shell, not a third picture.
        Shape {
            x: 0; y: 0; width: 300; height: 184
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "#4b5148"
                strokeWidth: 1
                fillColor: "#222923"
                startX: 72; startY: 154
                PathLine { x: 228; y: 154 }
                PathLine { x: 242; y: 179 }
                PathLine { x: 58; y: 179 }
                PathLine { x: 72; y: 154 }
            }
        }
        Rectangle { x: 97; y: 168; width: 106; height: 6; radius: 2; color: "#0e1510" }
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                x: index === 0 ? 78 : 214
                y: 166
                width: 9; height: 9; radius: 4.5
                color: "#0e1510"
                border.color: "#596252"
                border.width: 1
            }
        }

        Repeater {
            model: [Qt.point(8, 8), Qt.point(284, 8), Qt.point(8, 166), Qt.point(284, 166)]
            delegate: Rectangle {
                required property point modelData
                x: modelData.x; y: modelData.y
                width: 8; height: 8; radius: 4
                gradient: Gradient {
                    GradientStop { position: 0; color: "#9b9e8e" }
                    GradientStop { position: 1; color: "#434a40" }
                }
                border.color: "#151c15"
                border.width: 1
                Rectangle {
                    anchors.centerIn: parent
                    width: 4; height: 1; rotation: -35
                    color: "#182017"
                }
            }
        }
        Rectangle { x: 19; y: 181; width: 262; height: 1; color: "#73776b"; opacity: 0.42 }
    }
}
