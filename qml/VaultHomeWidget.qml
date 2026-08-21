pragma ComponentBehavior: Bound

// VaultHomeWidget - the Vault HOME mode-intro widget: a monumental mechanical vault portal.
// It is deliberately independent of Vault roots, scans, indexed rows, and local-media state.
// The object represents the app itself, so an empty first-run Vault renders identically to a
// populated one. Hover turns the handwheel, retracts the locking bars, and cracks the door open.

import QtQuick

Glass {
    id: vault
    objectName: "vaultHomeWidget"

    property string heading: "Vault"
    signal clicked()

    radius: 18
    height: 400

    Theme { id: theme }

    readonly property bool hovered: hit.containsMouse

    Text {
        anchors.top: parent.top; anchors.topMargin: 28
        anchors.horizontalCenter: parent.horizontalCenter
        text: vault.heading; color: theme.ink
        font.family: theme.display; font.pixelSize: 33
    }

    Text {
        anchors.left: parent.left; anchors.leftMargin: 46
        anchors.top: parent.top; anchors.topMargin: 36
        text: "On this machine"; color: theme.inkDim
        font.family: theme.display; font.italic: true; font.pixelSize: 22
    }

    Item {
        id: portal
        anchors.horizontalCenter: parent.horizontalCenter
        y: 82
        width: 820; height: 318

        // Ground shadow: two cheap plates instead of another blur/FBO.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 207
            width: 560; height: 86; radius: 43
            color: Qt.rgba(0, 0, 0, 0.30)
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 220
            width: 440; height: 62; radius: 31
            color: Qt.rgba(0, 0, 0, 0.22)
        }

        // The permanent steel housing. No identity text is engraved into it.
        Rectangle {
            id: frame
            anchors.horizontalCenter: parent.horizontalCenter
            width: 520; height: 312; radius: 28
            color: Qt.rgba(1, 1, 1, 0.045)
            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
            Rectangle {
                anchors.fill: parent; anchors.margins: 18; radius: 20
                color: "transparent"
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.075)
            }

            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    width: 7; height: 7; radius: 3.5
                    color: Qt.rgba(1, 1, 1, 0.10)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.10)
                    x: index % 2 === 0 ? 30 : frame.width - width - 30
                    y: index < 2 ? 26 : frame.height - height - 24
                }
            }

            // Deep circular throat behind the slab. The gold seam only appears while opening.
            Rectangle {
                anchors.centerIn: parent
                width: 282; height: 282; radius: 141
                color: Qt.rgba(0, 0, 0, 0.56)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.09)
            }
            Rectangle {
                anchors.centerIn: parent
                width: 242; height: 242; radius: 121
                color: Qt.rgba(0, 0, 0, 0.54)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.055)
            }
            Rectangle {
                anchors.centerIn: parent
                width: 222; height: 222; radius: 111
                color: "transparent"
                border.width: 2; border.color: theme.gold
                opacity: vault.hovered ? 0.50 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
            }

            // Right-side hinge mass makes the object read as a real door, not floating circles.
            Rectangle {
                x: frame.width / 2 + 120; y: 54
                width: 42; height: 206; radius: 10
                color: Qt.rgba(1, 1, 1, 0.055)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.10)
            }
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    x: frame.width / 2 + 42
                    y: 70 + index * 73
                    width: 112; height: 34; radius: 8
                    color: Qt.rgba(1, 1, 1, 0.075)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                    Rectangle {
                        anchors.right: parent.right; anchors.rightMargin: -11
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24; height: 24; radius: 12
                        color: Qt.rgba(1, 1, 1, 0.075)
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                    }
                }
            }
            Item {
                id: doorStage
                anchors.centerIn: parent
                width: 254; height: 254

                Item {
                    id: slab
                    anchors.fill: parent
                    transform: [
                        Translate {
                            x: vault.hovered ? -8 : 0
                            Behavior on x { NumberAnimation { duration: 520; easing.type: Easing.OutCubic } }
                        },
                        Rotation {
                            origin.x: slab.width; origin.y: slab.height / 2
                            axis { x: 0; y: 1; z: 0 }
                            angle: vault.hovered ? -16 : 0
                            Behavior on angle { NumberAnimation { duration: 520; easing.type: Easing.OutCubic } }
                        }
                    ]

                    Rectangle {
                        x: -8; y: -8
                        width: parent.width + 16; height: parent.height + 16; radius: width / 2
                        color: Qt.rgba(0, 0, 0, 0.38)
                        border.width: 8; border.color: Qt.rgba(0, 0, 0, 0.34)
                    }

                    Rectangle {
                        id: face
                        anchors.fill: parent; radius: width / 2
                        color: Qt.rgba(1, 1, 1, 0.065)
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.18)
                    }
                    // Machining rings: visible structure without typography or logo marks.
                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            anchors.centerIn: face
                            width: 220 - index * 38
                            height: width; radius: width / 2
                            color: "transparent"
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.045 + index * 0.012)
                        }
                    }

                    // Eight radial locking bars. Hover retracts every bar toward the centre.
                    Repeater {
                        model: 8
                        Item {
                            required property int index
                            readonly property real a: index * Math.PI / 4
                            width: 26; height: 54
                            x: face.width / 2 - width / 2 + Math.sin(a) * 103
                            y: face.height / 2 - height / 2 - Math.cos(a) * 103
                            rotation: index * 45

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: vault.hovered ? 7 : -10
                                width: 14; height: 38; radius: 4
                                color: Qt.rgba(1, 1, 1, 0.105)
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.13)
                                Behavior on y {
                                    NumberAnimation { duration: 270; easing.type: Easing.OutCubic }
                                }
                            }
                        }
                    }
                    // Five-spoke handwheel. This is the primary hover tell before the slab moves.
                    Item {
                        id: wheel
                        anchors.centerIn: parent
                        width: 116; height: 116
                        rotation: vault.hovered ? 62 : 0
                        Behavior on rotation {
                            NumberAnimation { duration: 380; easing.type: Easing.OutCubic }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 82; height: 82; radius: 41
                            color: "transparent"
                            border.width: 5; border.color: Qt.rgba(1, 1, 1, 0.11)
                        }

                        Repeater {
                            model: 5
                            Rectangle {
                                required property int index
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 8
                                width: 8; height: 54; radius: 4
                                transformOrigin: Item.Bottom
                                rotation: index * 72
                                color: Qt.rgba(1, 1, 1, 0.12)
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
                            }
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: -1; width: 18; height: 18; radius: 9
                            color: Qt.rgba(1, 1, 1, 0.13)
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.14)
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 34; height: 34; radius: 17
                            color: Qt.rgba(1, 1, 1, 0.095)
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.17)
                        }
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: face.height * 0.69
                        width: 38; height: 16; radius: 8
                        color: Qt.rgba(0, 0, 0, 0.12)
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.09)
                        Rectangle {
                            anchors.centerIn: parent
                            width: 4; height: 7; radius: 2
                            color: Qt.rgba(1, 1, 1, 0.16)
                        }
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: 11
                width: 330; height: 1
                color: Qt.rgba(1, 1, 1, 0.055)
            }
        }
    }

    MouseArea {
        id: hit
        objectName: "vaultHomeWidgetHitArea"
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: vault.clicked()
    }
}
