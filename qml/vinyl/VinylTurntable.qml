pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

Item {
    id: player

    property url artwork
    property bool cued: false
    property bool spinning: false
    property bool animateArm: true
    property color stylusColor

    width: 340
    height: 225

    Rectangle {
        width: 227; height: 225; radius: 113
        x: 15; y: 3
        color: "#a0000000"
    }
    Rectangle {
        id: platter
        x: 16
        width: 225; height: width; radius: width / 2
        border.color: "#72737a"
        gradient: Gradient {
            GradientStop { position: 0; color: "#666870" }
            GradientStop { position: 0.37; color: "#25262b" }
            GradientStop { position: 0.86; color: "#51525a" }
            GradientStop { position: 1; color: "#26272b" }
        }

        Rectangle {
            id: record
            objectName: "vinylHomeRecord"
            anchors.centerIn: parent
            width: 215; height: width; radius: width / 2
            border.color: "#25262a"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#070708" }
                GradientStop { position: 0.22; color: "#16171a" }
                GradientStop { position: 0.52; color: "#09090a" }
                GradientStop { position: 0.83; color: "#131416" }
                GradientStop { position: 1; color: "#070708" }
            }
            NumberAnimation on rotation {
                from: 0; to: 360; duration: 1800
                loops: Animation.Infinite
                running: true
                paused: !player.spinning
            }
            Repeater {
                model: 24
                delegate: Rectangle {
                    required property int index
                    anchors.fill: record
                    anchors.margins: 4 + index * 2.75
                    radius: width / 2
                    color: "transparent"
                    border.color: index % 6 === 0 ? "#25d6d9e4" : "#13d6d9e4"
                }
            }

            Rectangle {
                id: label
                anchors.centerIn: parent
                width: 65; height: width; radius: width / 2
                color: "#745441"
                border.color: "#5affffff"

                Image {
                    id: cover
                    anchors.fill: parent
                    source: player.artwork
                    sourceSize: Qt.size(130, 130)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: false
                }
                Item {
                    id: labelMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: labelArt.active
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "white"
                    }
                }
                Loader {
                    id: labelArt
                    anchors.fill: parent
                    active: cover.status === Image.Ready && GraphicsInfo.api !== GraphicsInfo.Software
                    sourceComponent: MultiEffect {
                        source: cover
                        autoPaddingEnabled: false
                        maskEnabled: true
                        maskSource: labelMask
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 7; height: width; radius: width / 2
                    color: "#b9bbc2"
                    border.color: "#5b5e67"
                }
            }
        }
    }

    Rectangle {
        x: 280; y: 14
        width: 52; height: width; radius: width / 2
        border.color: "#74767d"
        gradient: Gradient {
            GradientStop { position: 0; color: "#666871" }
            GradientStop { position: 0.45; color: "#43454b" }
            GradientStop { position: 1; color: "#26272c" }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 30; height: width; radius: width / 2
            color: "#34363b"
            border.color: "#5b5d64"
        }
    }

    Rectangle {
        x: 307; y: 163
        width: 12; height: 30; radius: 4
        color: "#27282d"
        border.color: "#51535b"
    }

    Item {
        id: arm
        objectName: "vinylHomeTonearm"
        x: 294; y: 40
        width: 24; height: 162

        transform: Rotation {
            origin.x: 12
            origin.y: 0
            angle: player.spinning ? 42 : -5
            Behavior on angle {
                enabled: player.animateArm
                NumberAnimation {
                    duration: player.spinning ? 540 : 620
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.24, 0.76, 0.25, 1, 1, 1]
                }
            }
        }

        MetalPart { x: 4; y: -15; width: 16; height: 25; radius: 4 }
        MetalPart { x: 9; y: 1; width: 6; height: 131; radius: 3 }
        MetalPart { x: 7; y: 18; width: 10; height: 9; radius: 2 }
        MetalPart { x: 7; y: 123; width: 11; height: 20; radius: 3; rotation: 12 }
        Rectangle {
            x: 3; y: 140
            width: 18; height: 22; radius: 4; rotation: 13
            border.color: "#82848c"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#2b2c33" }
                GradientStop { position: 0.45; color: "#64666f" }
                GradientStop { position: 1; color: "#2b2c33" }
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: 10; height: 3; radius: 1
                color: player.stylusColor
                opacity: player.cued ? 1 : 0.78
            }
        }
    }

    Text {
        x: 260; y: 199
        text: "•  33⅓"
        color: "#b9b9c0"
        font.pixelSize: 10
    }

    component MetalPart: Rectangle {
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#3a3c41" }
            GradientStop { position: 0.33; color: "#a9abb2" }
            GradientStop { position: 0.63; color: "#767880" }
            GradientStop { position: 1; color: "#3b3d43" }
        }
    }
}
