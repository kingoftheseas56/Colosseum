import QtQuick
import ".."

// Artist imagery identifies the station; the host owns its name and playback.
Item {
    id: root
    property url artworkSource: ""
    property string title: ""
    implicitWidth: 196
    implicitHeight: width

    readonly property real objectScale: Math.max(0, Math.min(width / 220, height / 220))
    Theme { id: theme }

    Item {
        id: radio
        width: 220
        height: 220
        x: (root.width - width * scale) / 2
        y: (root.height - height * scale) / 2
        scale: root.objectScale
        transformOrigin: Item.TopLeft

        // Open carrying handle: the space between its three pieces stays clear.
        Rectangle { x: 70; y: 8; width: 8; height: 24; radius: 3; color: "#2c2c2c"; border.color: "#6f6f6f" }
        Rectangle { x: 142; y: 8; width: 8; height: 24; radius: 3; color: "#2c2c2c"; border.color: "#6f6f6f" }
        Rectangle {
            x: 70; y: 2; width: 80; height: 11; radius: 6
            gradient: Gradient {
                GradientStop { position: 0; color: "#6d6d6d" }
                GradientStop { position: 0.3; color: "#383838" }
                GradientStop { position: 1; color: "#212121" }
            }
            border.color: "#898989"
            border.width: 1
        }
        Rectangle {
            x: 0; y: 22; width: 220; height: 198; radius: 17
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
            x: 5; y: 27; width: 210; height: 188; radius: 13
            color: "transparent"
            border.color: "#222222"
            border.width: 2
        }
        Rectangle {
            x: 11; y: 33; width: 198; height: 176; radius: 9
            gradient: Gradient {
                GradientStop { position: 0; color: "#474747" }
                GradientStop { position: 0.6; color: "#343434" }
                GradientStop { position: 1; color: "#2a2a2a" }
            }
            border.color: "#707070"
            border.width: 1
        }
        Item {
            id: speaker
            x: 21; y: 141; width: 62; height: 62
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
                anchors.margins: 3
                radius: width / 2
                color: "#1c1c1c"
                border.color: "#777777"
                border.width: 1
            }
            // Chord lengths follow the circle; no rounded-Rectangle clipping assumption.
            Repeater {
                model: 9
                delegate: Rectangle {
                    required property int index
                    readonly property real offsetFromCentre: (index - 4) * 5.6
                    readonly property real halfChord: Math.sqrt(Math.max(0, 25 * 25 - offsetFromCentre * offsetFromCentre))
                    x: speaker.width / 2 - halfChord
                    y: speaker.height / 2 + offsetFromCentre - 1
                    width: halfChord * 2
                    height: 2
                    radius: 1
                    color: index % 2 === 0 ? "#676767" : "#4a4a4a"
                }
            }
        }

        // The station portrait is almost as large as the speaker, never a tiny badge.
        Rectangle {
            x: 19; y: 41; width: 182; height: 98; radius: 3
            color: "#191919"
            border.color: "#a1a1a1"
            border.width: 1
        }
        VinylArtwork {
            x: 23; y: 45; width: 174; height: 90
            source: root.artworkSource
            crop: true
            backgroundColor: "#313131"
        }
        Rectangle {
            x: 21; y: 43; width: 178; height: 94
            color: "transparent"
            border.color: "#090909"
            border.width: 2
        }
        Rectangle {
            x: 92; y: 160; width: 92; height: 9; radius: 3
            color: "#1c1c1c"
            border.color: "#838383"
            border.width: 1
            Repeater {
                model: 16
                delegate: Rectangle {
                    required property int index
                    x: 5 + index * 7
                    y: 2
                    width: 1
                    height: index % 4 === 0 ? 5 : 3
                    color: "#bebebe"
                    opacity: index % 4 === 0 ? 0.8 : 0.43
                }
            }
            Rectangle { x: 58; y: 1; width: 2; height: 7; color: "#dbdbdb" }
        }
        Rectangle {
            x: 188; y: 155; width: 20; height: 20; radius: 10
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
                anchors.margins: 3
                radius: width / 2
                color: "#444444"
                border.color: "#b0b0b0"
                border.width: 1
            }
        }
        Text {
            x: 92; y: 180; width: 116; height: 10
            text: qsTr("FM · ENDLESS")
            font.family: theme.ui; font.pixelSize: 8; font.weight: Font.DemiBold
            font.letterSpacing: 1.4; color: "#9a9a9a"
            elide: Text.ElideRight
        }
    }
}
