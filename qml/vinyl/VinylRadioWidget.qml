import QtQuick

// Artist imagery identifies the station; the host owns its name and playback.
Item {
    id: root
    property url artworkSource: ""
    property string title: ""
    implicitWidth: 196
    implicitHeight: width * 224 / 304

    readonly property real objectScale: Math.max(0, Math.min(width / 304, height / 224))

    Item {
        id: radio
        width: 300
        height: 220
        x: (root.width - width * scale) / 2
        y: (root.height - height * scale) / 2
        scale: root.objectScale
        transformOrigin: Item.TopLeft

        // Open carrying handle: the space between its three pieces stays clear.
        Rectangle { x: 85; y: 15; width: 9; height: 28; radius: 3; color: "#2c2c2c"; border.color: "#6f6f6f" }
        Rectangle { x: 206; y: 15; width: 9; height: 28; radius: 3; color: "#2c2c2c"; border.color: "#6f6f6f" }
        Rectangle {
            x: 85; y: 10; width: 130; height: 12; radius: 6
            gradient: Gradient {
                GradientStop { position: 0; color: "#6d6d6d" }
                GradientStop { position: 0.3; color: "#383838" }
                GradientStop { position: 1; color: "#212121" }
            }
            border.color: "#898989"
            border.width: 1
        }
        Rectangle { x: 42; y: 207; width: 37; height: 9; radius: 3; color: "#1a1a1a" }
        Rectangle { x: 221; y: 207; width: 37; height: 9; radius: 3; color: "#1a1a1a" }

        Rectangle {
            x: 0; y: 34; width: 300; height: 177; radius: 17
            gradient: Gradient {
                GradientStop { position: 0; color: "#666666" }
                GradientStop { position: 0.035; color: "#494949" }
                GradientStop { position: 0.72; color: "#343434" }
                GradientStop { position: 1; color: "#262626" }
            }
            border.color: "#868686"
            border.width: 1
        }
        Rectangle {
            x: 5; y: 39; width: 290; height: 166; radius: 13
            color: "transparent"
            border.color: "#222222"
            border.width: 2
        }
        Rectangle {
            x: 12; y: 46; width: 276; height: 152; radius: 9
            gradient: Gradient {
                GradientStop { position: 0; color: "#474747" }
                GradientStop { position: 0.6; color: "#343434" }
                GradientStop { position: 1; color: "#2a2a2a" }
            }
            border.color: "#707070"
            border.width: 1
        }
        Rectangle { x: 22; y: 48; width: 256; height: 1; color: "#aaaaaa"; opacity: 0.34 }

        Item {
            id: speaker
            x: 24; y: 59; width: 117; height: 117
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#828282" }
                    GradientStop { position: 0.1; color: "#393939" }
                    GradientStop { position: 0.9; color: "#1b1b1b" }
                    GradientStop { position: 1; color: "#7a7a7a" }
                }
                border.color: "#1d1d1d"
                border.width: 1
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 5
                radius: width / 2
                color: "#1c1c1c"
                border.color: "#777777"
                border.width: 1
            }
            // Chord lengths follow the circle; no rounded-Rectangle clipping assumption.
            Repeater {
                model: 15
                delegate: Rectangle {
                    required property int index
                    readonly property real offsetFromCentre: (index - 7) * 6.4
                    readonly property real halfChord: Math.sqrt(Math.max(0, 49 * 49 - offsetFromCentre * offsetFromCentre))
                    x: speaker.width / 2 - halfChord
                    y: speaker.height / 2 + offsetFromCentre - 1
                    width: halfChord * 2
                    height: 2
                    radius: 1
                    color: index % 2 === 0 ? "#676767" : "#4a4a4a"
                }
            }
            Repeater {
                model: 13
                delegate: Rectangle {
                    required property int index
                    readonly property real offsetFromCentre: (index - 6) * 7.2
                    readonly property real halfChord: Math.sqrt(Math.max(0, 48 * 48 - offsetFromCentre * offsetFromCentre))
                    x: speaker.width / 2 + offsetFromCentre
                    y: speaker.height / 2 - halfChord
                    width: 1
                    height: halfChord * 2
                    color: "#181818"
                    opacity: 0.68
                }
            }
        }

        // The station portrait is almost as large as the speaker, never a tiny badge.
        Rectangle {
            x: 154; y: 55; width: 122; height: 112; radius: 3
            color: "#191919"
            border.color: "#a1a1a1"
            border.width: 1
        }
        VinylArtwork {
            x: 159; y: 60; width: 112; height: 102
            source: root.artworkSource
            crop: true
            backgroundColor: "#313131"
        }
        Rectangle {
            x: 157; y: 58; width: 116; height: 106
            color: "transparent"
            border.color: "#090909"
            border.width: 2
        }
        Rectangle { x: 160; y: 60; width: 109; height: 1; color: "#e9e9e9"; opacity: 0.26 }

        Rectangle {
            x: 25; y: 182; width: 207; height: 9; radius: 3
            color: "#1c1c1c"
            border.color: "#838383"
            border.width: 1
            Repeater {
                model: 29
                delegate: Rectangle {
                    required property int index
                    x: 6 + index * 6.9
                    y: 2
                    width: 1
                    height: index % 4 === 0 ? 5 : 3
                    color: "#bebebe"
                    opacity: index % 4 === 0 ? 0.8 : 0.43
                }
            }
            Rectangle { x: 132; y: 1; width: 2; height: 7; color: "#dbdbdb" }
        }
        Rectangle {
            x: 247; y: 174; width: 23; height: 23; radius: 11.5
            gradient: Gradient {
                GradientStop { position: 0; color: "#b6b6b6" }
                GradientStop { position: 0.22; color: "#838383" }
                GradientStop { position: 0.75; color: "#464646" }
                GradientStop { position: 1; color: "#212121" }
            }
            border.color: "#151515"
            border.width: 1
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: width / 2
                color: "#444444"
                border.color: "#b0b0b0"
                border.width: 1
            }
            Rectangle { x: 11; y: 4; width: 1; height: 4; color: "#e4e4e4" }
        }
        Rectangle { x: 22; y: 207; width: 256; height: 1; color: "#9e9e9e"; opacity: 0.28 }
    }
}
