pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

Item {
    id: scenes

    required property int sceneIndex
    required property bool motion
    required property bool reducedMotion

    function art(name: string): url {
        return Qt.resolvedUrl("../assets/startup/" + name)
    }

    Loader {
        anchors.fill: parent
        asynchronous: true
        sourceComponent: [vinylScene, tankobanScene, theatreScene, biblioScene][scenes.sceneIndex]
    }

    Component {
        id: vinylScene
        Loader {
            id: deck
            y: -5
            Component.onCompleted: setSource("vinyl/VinylHomeWidget.qml", {
                backdrop: null,
                tint: 0,
                scrim: 0,
                edge: "transparent",
                loadingPresentation: true,
                localArtworkDirectory: Qt.resolvedUrl("../assets/startup/vinyl/"),
                reducedMotion: Qt.binding(() => scenes.reducedMotion),
                cueRequested: Qt.binding(() => scenes.motion && cueDelay.cued)
            })
            Timer {
                id: cueDelay
                property bool cued: false
                running: deck.status === Loader.Ready && scenes.motion
                interval: 245
                onTriggered: cued = true
            }
        }
    }

    Component {
        id: tankobanScene
        Item {
            CoverFan {
                x: 37; y: 49; width: 460; height: 310
                names: ["manga/one-piece.jpg", "manga/berserk.jpg", "manga/vinland-saga.jpg", "manga/vagabond.jpg", "manga/chainsaw-man.jpg"]
            }
            CoverFan {
                x: 583; y: 49; width: 460; height: 310
                names: ["comics/invincible.jpg", "comics/sandman.jpg", "comics/saga.jpg", "comics/watchmen.jpg", "comics/sin-city.jpg"]
            }
        }
    }

    component CoverFan: Item {
        id: fan
        required property var names
        property real spread: scenes.reducedMotion ? 68 : 53

        NumberAnimation on spread {
            running: scenes.motion
            to: 68
            duration: 1350
            easing.type: Easing.OutCubic
        }
        Repeater {
            model: fan.names
            delegate: Item {
                id: leaf
                required property int index
                required property string modelData
                readonly property int offset: index - 2
                width: offset === 0 ? 165 : 149
                height: width * 1.52
                x: (fan.width - width) / 2 + offset * fan.spread
                y: (fan.height - height) / 2 + Math.abs(offset) * 12
                rotation: offset * 7.5
                transformOrigin: Item.Bottom
                z: 4 - Math.abs(offset)
                RectangularShadow { anchors.fill: parent; radius: 4; blur: 17; offset.y: 9; color: "#c8000000" }
                Rectangle { anchors.fill: parent; radius: 4; color: "#28232a" }
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: scenes.art(leaf.modelData)
                    sourceSize: Qt.size(340, 510)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Rectangle { anchors.fill: parent; radius: 4; color: "transparent"; border.color: "#52e0d1c6" }
            }
        }
    }

    Component {
        id: theatreScene
        Item {
            id: cinema
            readonly property var stills: ["theatre/dune-part-two.jpg", "theatre/shogun.jpg", "theatre/oppenheimer.jpg", "theatre/the-bear.jpg", "theatre/interstellar.jpg"]
            RectangularShadow { anchors.fill: strip; radius: 6; blur: 27; offset.y: 13; color: "#b5000000" }
            Rectangle {
                id: strip
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 239
                color: "#111014"
                radius: 8
                border.color: "#755c5046"
                clip: true
                Row {
                    id: frames
                    y: 24
                    spacing: 10
                    x: -82
                    Repeater {
                        model: cinema.stills
                        delegate: Image {
                            required property string modelData
                            width: 314
                            height: 191
                            source: scenes.art(modelData)
                            sourceSize: Qt.size(640, 385)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                    NumberAnimation on x {
                        running: scenes.motion
                        from: -82; to: -239; duration: 9000
                    }
                }
                Sprockets { anchors.left: parent.left; anchors.right: parent.right; y: 8 }
                Sprockets { anchors.left: parent.left; anchors.right: parent.right; y: 222 }
            }
        }
    }

    component Sprockets: Item {
        height: 9
        Row {
            x: 10; spacing: 14
            Repeater {
                model: 53
                delegate: Rectangle { width: 7; height: 8; radius: 1.5; color: "#7b746b" }
            }
        }
    }

    Component {
        id: biblioScene
        Item {
            BookObject {
                x: 215; y: 60
                source: scenes.art("biblio/regime-change.jpg")
                rotation: -8
            }
            Rectangle {
                id: compactDisc
                x: 664; y: 70
                width: 221; height: width; radius: width / 2
                border.color: "#b4bcb9af"
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#5d636e" }
                    GradientStop { position: 0.24; color: "#c3afad" }
                    GradientStop { position: 0.45; color: "#4e535f" }
                    GradientStop { position: 0.57; color: "#c4d4ca" }
                    GradientStop { position: 0.69; color: "#b3a1ba" }
                    GradientStop { position: 1; color: "#495267" }
                }
                RectangularShadow { anchors.fill: parent; radius: parent.radius; z: -1; blur: 20; offset.y: 10; color: "#af000000" }
                Repeater {
                    model: 12
                    delegate: Rectangle {
                        required property int index
                        anchors.fill: compactDisc
                        anchors.margins: 5 + index * 7
                        radius: width / 2
                        color: "transparent"
                        border.color: index % 4 === 0 ? "#3a15192a" : "#10ffffff"
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 31; height: width; radius: width / 2
                    color: "#1b2230"; border.width: 5; border.color: "#8da3a3a9"
                }
                RotationAnimator on rotation {
                    running: scenes.motion
                    from: 0; to: 360; duration: 9200; loops: Animation.Infinite
                }
            }
            BookObject {
                x: 587; y: 101
                source: scenes.art("biblio/whistler.jpg")
                rotation: -5
                width: 190; height: 190
            }
        }
    }

    component BookObject: Item {
        id: book
        required property url source
        width: 206
        height: 288
        RectangularShadow { anchors.fill: parent; radius: 6; blur: 19; offset.y: 12; color: "#c6000000" }
        Rectangle {
            x: 5; y: 7; width: book.width + 2; height: book.height - 2; radius: 4
            gradient: Gradient {
                GradientStop { position: 0; color: "#3c3432" }
                GradientStop { position: 0.86; color: "#5b5450" }
                GradientStop { position: 1; color: "#bfb39f" }
            }
        }
        Image {
            width: book.width; height: book.height
            source: book.source
            fillMode: Image.PreserveAspectCrop
            sourceSize: Qt.size(410, 570)
            asynchronous: true
        }
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#40efe0cb"; radius: 4 }
    }
}
