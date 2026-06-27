// PersonalizePage - PROTOTYPE (throwaway design probe, run standalone, no compile):
//   native\build\colosseum.exe qml\PersonalizePage.qml
// The wallpaper page in REAL material: the live wallpaper IS the page; a floating frosted Glass
// console (left) holds ONLY the gallery (masthead, search, category lanes, grid); the focused
// wallpaper's identity + Apply live on the wallpaper side (right), so the console never clutters.
// Hover a tile -> its identity updates live on the right (the seed of PS4 "try it on live").

import QtQuick
import QtQuick.Window
import QtQuick.Layouts

Window {
    id: root
    width: 1460; height: 880
    visible: true
    color: "#05060a"
    title: "Colosseum - Personalize (prototype)"

    Theme { id: theme }
    readonly property string mono: "Consolas"

    property var tiles: [
        { name: "Grand Line - sunrise", spec: "3840x2160   -   MOTION 12s loop   -   AniList", motion: true,  a: "#f0a64a", b: "#16527a" },
        { name: "Wano",                 spec: "3840x2160   -   Still   -   AniList",            motion: false, a: "#8a3f7a", b: "#3a1d52" },
        { name: "Marineford",           spec: "3840x2160   -   MOTION 9s loop   -   AniList",   motion: true,  a: "#3a5fa0", b: "#10243f" },
        { name: "Skypiea",              spec: "3840x2160   -   Still   -   AniList",            motion: false, a: "#1f9f7a", b: "#0c2e26" },
        { name: "Dressrosa",            spec: "3840x2160   -   Still   -   AniList",            motion: false, a: "#c46a6a", b: "#2a1530" },
        { name: "Punk Hazard - embers", spec: "3840x2160   -   MOTION 14s loop   -   AniList",  motion: true,  a: "#e0763a", b: "#2a0e1a" },
        { name: "Fish-Man Island",      spec: "3840x2160   -   Still   -   AniList",            motion: false, a: "#2a6a8a", b: "#102a4a" },
        { name: "Monochrome",           spec: "3840x2160   -   Still   -   Wallhaven",          motion: false, a: "#8a8a92", b: "#1a1a24" }
    ]
    property int current: 0
    property int activeLane: 0

    Item {
        id: wall
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.00; color: "#123a57" }
                GradientStop { position: 0.50; color: "#1a6a92" }
                GradientStop { position: 1.00; color: "#2a93b0" }
            }
        }
        Rectangle {
            width: parent.width; height: parent.height * 0.5
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.94, 0.70, 0.32, 0.55) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.00; color: Qt.rgba(0, 0, 0, 0.62) }
                GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.14) }
                GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.00) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.42) }
            }
        }
    }

    Column {
        anchors.right: parent.right; anchors.rightMargin: 50
        anchors.top: parent.top; anchors.topMargin: 34
        spacing: 8
        Row {
            spacing: 15
            Text {
                text: "DRESSING"; color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.5
                anchors.verticalCenter: parent.verticalCenter
            }
            Repeater {
                model: [ "Home", "Tankoban", "Theatre", "Biblio", "Music" ]
                Rectangle {
                    required property int index
                    width: 12; height: 12; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: index === 1 ? theme.gold : Qt.rgba(1, 1, 1, 0.26)
                    border.width: 1
                    border.color: index === 1 ? theme.gold : Qt.rgba(1, 1, 1, 0.40)
                }
            }
        }
        Text {
            anchors.right: parent.right
            text: "Tankoban"; color: theme.gold
            font.family: theme.display; font.pixelSize: 16
        }
    }

    ColumnLayout {
        anchors.left: panel.right; anchors.leftMargin: 64
        anchors.right: parent.right; anchors.rightMargin: 64
        anchors.bottom: parent.bottom; anchors.bottomMargin: 54
        spacing: 14
        Text {
            text: root.tiles[root.current].name
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
            Layout.fillWidth: true
        }
        Text {
            text: root.tiles[root.current].spec
            color: theme.inkDim; font.family: root.mono; font.pixelSize: 13; font.letterSpacing: 0.5
        }
        RowLayout {
            spacing: 14
            Rectangle {
                Layout.preferredWidth: 230; Layout.preferredHeight: 50; radius: 26
                color: applyMa.containsMouse ? "#ffd864" : theme.gold
                Text {
                    anchors.centerIn: parent; text: "Apply to Tankoban"
                    color: "#1a1206"; font.family: theme.ui; font.pixelSize: 14; font.bold: true
                }
                MouseArea { id: applyMa; anchors.fill: parent; hoverEnabled: true }
            }
            Rectangle {
                Layout.preferredWidth: 138; Layout.preferredHeight: 50; radius: 26
                color: "transparent"
                border.width: 1; border.color: allMa.containsMouse ? Qt.rgba(1,1,1,0.34) : theme.edge
                Text {
                    anchors.centerIn: parent; text: "All worlds >"
                    color: allMa.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 14
                }
                MouseArea { id: allMa; anchors.fill: parent; hoverEnabled: true }
            }
        }
    }

    Glass {
        id: panel
        backdrop: wall
        radius: 24
        x: 40; y: 40
        width: Math.min(parent.width * 0.40, 560)
        height: parent.height - 80
        tint: 0.05
        scrim: 0.30
        blurAmount: 1.0

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 38
            spacing: 18

            Text {
                text: "PERSONALIZE"; color: theme.gold
                font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 5; font.bold: true
            }
            Text {
                text: "Tankoban"; color: theme.ink
                font.family: theme.display; font.pixelSize: 46
                Layout.topMargin: -6
            }
            Text {
                text: "Choose this world's wallpaper. Hover to try it on live - apply when it feels like home."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                lineHeight: 1.35; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 48; Layout.topMargin: 4
                radius: 24; color: Qt.rgba(0, 0, 0, 0.34)
                border.width: 1; border.color: theme.edge
                Row {
                    anchors.fill: parent; anchors.leftMargin: 18; spacing: 10
                    Image {
                        source: "../assets/icons/search.svg"
                        width: 16; height: 16; opacity: 0.55
                        anchors.verticalCenter: parent.verticalCenter
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: "Search wallpapers - a title, a mood, a colour"
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 22
                Repeater {
                    model: [ "Your universes", "Anime", "Series", "Music", "Abstract", "Motion" ]
                    Item {
                        required property int index
                        required property string modelData
                        implicitWidth: laneTxt.implicitWidth
                        implicitHeight: 26
                        Text {
                            id: laneTxt
                            text: parent.modelData
                            color: root.activeLane === parent.index ? theme.ink
                                 : (laneMa.containsMouse ? theme.inkDim : theme.inkDimmer)
                            font.family: theme.ui; font.pixelSize: 13
                        }
                        Rectangle {
                            anchors.top: laneTxt.bottom; anchors.topMargin: 5
                            width: laneTxt.implicitWidth; height: 2; radius: 1
                            color: theme.gold
                            visible: root.activeLane === parent.index
                        }
                        MouseArea {
                            id: laneMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.activeLane = parent.index
                        }
                    }
                }
            }

            GridView {
                id: grid
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.topMargin: 2
                clip: true
                cellWidth: width / 2
                cellHeight: cellWidth * 0.70
                model: root.tiles
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    required property int index
                    required property var modelData
                    width: grid.cellWidth; height: grid.cellHeight

                    Rectangle {
                        id: tile
                        anchors.fill: parent; anchors.margins: 7
                        radius: 12
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: modelData.a }
                            GradientStop { position: 1.0; color: modelData.b }
                        }
                        border.width: root.current === index ? 2 : 1
                        border.color: root.current === index ? theme.gold : Qt.rgba(1, 1, 1, 0.10)
                        scale: tileMa.containsMouse ? 1.03 : 1.0
                        Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

                        Rectangle {
                            anchors.fill: parent; anchors.margins: -3; radius: 15; z: -1
                            visible: root.current === index
                            color: "transparent"
                            border.width: 6; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.28)
                            SequentialAnimation on opacity {
                                running: root.current === index; loops: Animation.Infinite
                                NumberAnimation { from: 0.55; to: 1.0; duration: 1500; easing.type: Easing.InOutSine }
                                NumberAnimation { from: 1.0; to: 0.55; duration: 1500; easing.type: Easing.InOutSine }
                            }
                        }

                        Rectangle {
                            visible: modelData.motion
                            anchors.top: parent.top; anchors.left: parent.left
                            anchors.margins: 8
                            radius: 11; color: Qt.rgba(0, 0, 0, 0.45)
                            width: badge.implicitWidth + 16; height: 20
                            Row {
                                anchors.centerIn: parent; spacing: 4
                                Text { text: ">"; color: theme.gold; font.pixelSize: 8; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    id: badge; text: "MOTION"; color: theme.gold
                                    font.family: root.mono; font.pixelSize: 9; font.letterSpacing: 0.6
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        Text {
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 10
                            text: modelData.name; color: "white"; font.family: theme.ui; font.pixelSize: 12
                            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.6)
                        }

                        MouseArea {
                            id: tileMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: root.current = index
                        }
                    }
                }
            }
        }
    }
}
