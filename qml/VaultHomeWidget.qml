pragma ComponentBehavior: Bound

// VaultHomeWidget - the Vault HOME mode-intro widget: a solid mechanical archive hatch.
// It is deliberately independent of Vault roots, scans, indexed rows, and local-media state.
// The object represents the app itself, so an empty first-run Vault renders identically to a
// populated one. Hover turns the handwheel, retracts the locking bars, and cracks the door open.

import QtQuick

Glass {
    id: vault
    objectName: "vaultHomeWidget"

    property string heading: "Vault"
    readonly property bool compactLayout: width < 600
    signal clicked()

    radius: 18
    height: vault.compactLayout ? 440 : 520
    tint: 0.035
    scrim: 0.24

    Theme { id: theme }

    readonly property bool hovered: hit.containsMouse || keyboardAction.activeFocus

    Text {
        id: vaultTitle
        objectName: "vaultHomeWidgetTitle"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 28
        text: vault.heading
        color: theme.ink
        font.family: theme.display
        font.pixelSize: vault.compactLayout ? 30 : 33
    }

    Rectangle {
        anchors.top: vaultTitle.bottom
        anchors.topMargin: 10
        anchors.horizontalCenter: parent.horizontalCenter
        width: 64
        height: 2
        radius: 1
        color: theme.gold
    }

    Item {
        id: portal
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 78
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 54

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            width: Math.min(parent.width - (vault.compactLayout ? 32 : 96), 620)
            height: vault.compactLayout ? 42 : 54
            radius: 27
            color: Qt.rgba(0, 0, 0, 0.42)
        }

        Rectangle {
            id: cabinet
            anchors.centerIn: parent
            width: Math.min(parent.width - (vault.compactLayout ? 20 : 56), 760)
            height: Math.min(parent.height - 8, vault.compactLayout ? 300 : 366)
            radius: 12
            clip: true
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#39444a" }
                GradientStop { position: 0.18; color: "#1a2025" }
                GradientStop { position: 0.72; color: "#252e34" }
                GradientStop { position: 1.0; color: "#0d1215" }
            }
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.20)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 8
                radius: 8
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)
            }

            Rectangle {
                id: plate
                anchors.fill: parent
                anchors.margins: vault.compactLayout ? 12 : 20
                radius: 7
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#242c31" }
                    GradientStop { position: 0.48; color: "#171d21" }
                    GradientStop { position: 1.0; color: "#0e1316" }
                }
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.72)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                }

                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        width: 18
                        height: 18
                        radius: 9
                        x: index % 2 === 0 ? 20 : plate.width - width - 20
                        y: index < 2 ? 20 : plate.height - height - 20
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#78838a" }
                            GradientStop { position: 0.22; color: "#3a454b" }
                            GradientStop { position: 0.78; color: "#111619" }
                            GradientStop { position: 1.0; color: "#070a0c" }
                        }
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.20)
                    }
                }

                // Deep throat behind the door. The gold seam only appears while opening.
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(312, plate.height - 26)
                    height: width
                    radius: width / 2
                    color: "#080b0d"
                    border.width: 9
                    border.color: "#11171b"
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(286, plate.height - 42)
                    height: width
                    radius: width / 2
                    color: "#0b1013"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.16)
                }

                // Hinge spine and three heavy hinge knuckles on the right edge.
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.top: parent.top
                    anchors.topMargin: 42
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 42
                    width: 22
                    radius: 4
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#0a0e10" }
                        GradientStop { position: 0.34; color: "#68757c" }
                        GradientStop { position: 0.66; color: "#1c2428" }
                        GradientStop { position: 1.0; color: "#080b0d" }
                    }
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.12)
                }
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        anchors.right: plate.right
                        anchors.rightMargin: 18
                        y: 50 + index * ((plate.height - 100) / 2)
                        width: 34
                        height: 56
                        radius: 5
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#111619" }
                            GradientStop { position: 0.36; color: "#6a777d" }
                            GradientStop { position: 0.72; color: "#222b30" }
                            GradientStop { position: 1.0; color: "#080b0d" }
                        }
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.14)
                    }
                }

                Item {
                    id: door
                    anchors.centerIn: parent
                    property real doorSize: vault.compactLayout
                        ? Math.min(238, Math.max(132, Math.min(plate.height - 34, plate.width - 64)))
                        : Math.min(306, Math.max(188, Math.min(plate.height - 48, plate.width - 112)))
                    width: doorSize
                    height: doorSize
                    z: 3
                    transform: [
                        Translate {
                            x: vault.hovered ? -8 : 0
                            Behavior on x { NumberAnimation { duration: 440; easing.type: Easing.OutCubic } }
                        },
                        Rotation {
                            origin.x: door.width
                            origin.y: door.height / 2
                            axis { x: 0; y: 1; z: 0 }
                            angle: vault.hovered ? -9 : 0
                            Behavior on angle { NumberAnimation { duration: 440; easing.type: Easing.OutCubic } }
                        }
                    ]

                    Rectangle {
                        x: -11
                        y: -11
                        width: parent.width + 22
                        height: parent.height + 22
                        radius: width / 2
                        color: Qt.rgba(0, 0, 0, 0.64)
                        border.width: 8
                        border.color: "#050708"
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#4a565d" }
                            GradientStop { position: 0.28; color: "#252e33" }
                            GradientStop { position: 0.68; color: "#11171a" }
                            GradientStop { position: 1.0; color: "#313b40" }
                        }
                        border.width: 8
                        border.color: "#090c0e"
                    }

                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            anchors.centerIn: door
                            width: door.width - 24 - index * 38
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.width: index === 0 ? 1 : 2
                            border.color: Qt.rgba(1, 1, 1, 0.10 - index * 0.018)
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 10
                        radius: width / 2
                        color: "transparent"
                        border.width: 3
                        border.color: theme.gold
                        opacity: vault.hovered ? 0.88 : 0.08
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }

                    // Eight radial locking bars retract toward the centre on hover.
                    Repeater {
                        model: 8
                        Item {
                            required property int index
                            readonly property real angle: index * Math.PI / 4
                            width: 24
                            height: 64
                            x: door.width / 2 - width / 2 + Math.sin(angle) * (door.width / 2 - 30)
                            y: door.height / 2 - height / 2 - Math.cos(angle) * (door.height / 2 - 30)
                            rotation: index * 45

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: vault.hovered ? 8 : -12
                                width: 15
                                height: 50
                                radius: 4
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#7d898e" }
                                    GradientStop { position: 0.38; color: "#384349" }
                                    GradientStop { position: 1.0; color: "#111619" }
                                }
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.17)
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
                        width: Math.min(116, door.width * 0.44)
                        height: width
                        rotation: vault.hovered ? 68 : 0
                        Behavior on rotation {
                            NumberAnimation { duration: 380; easing.type: Easing.OutCubic }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "transparent"
                            border.width: 7
                            border.color: "#4e5b61"
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 6
                                radius: width / 2
                                color: "transparent"
                                border.width: 4
                                border.color: "#141b1f"
                            }
                        }

                        Repeater {
                            model: 5
                            Rectangle {
                                required property int index
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 9
                                width: 9
                                height: wheel.height * 0.46
                                radius: 4
                                transformOrigin: Item.Bottom
                                rotation: index * 72
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#8b969a" }
                                    GradientStop { position: 0.32; color: "#48555b" }
                                    GradientStop { position: 1.0; color: "#1a2226" }
                                }
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.16)
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 32
                            height: 32
                            radius: 16
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#9aa3a4" }
                                GradientStop { position: 0.35; color: "#4a565a" }
                                GradientStop { position: 1.0; color: "#12181b" }
                            }
                            border.width: 3
                            border.color: "#111619"
                        }
                    }
                }
            }
        }
    }

    Text {
        id: machineLabel
        objectName: "vaultHomeWidgetLocationLabel"
        anchors.left: parent.left
        anchors.leftMargin: 46
        anchors.top: parent.top
        anchors.topMargin: 36
        visible: !vault.compactLayout
        text: "On this machine"
        color: theme.inkDim
        font.family: theme.display
        font.italic: true
        font.pixelSize: 22
    }

    MouseArea {
        id: hit
        objectName: "vaultHomeWidgetHitArea"
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: vault.clicked()
    }
    KeyboardAction {
        id: keyboardAction
        objectName: "vaultHomeWidgetKeyboardAction"
        anchors.fill: parent
        pointerEnabled: false
        accessibleName: "Open Vault"
        onTriggered: vault.clicked()
    }
}
