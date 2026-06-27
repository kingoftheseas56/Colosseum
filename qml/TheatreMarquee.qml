// TheatreMarquee - the HOME mode-intro widget for Theatre.
// It borrows Harbor's big backdrop + row grammar, but stays a Colosseum widget:
// glass chrome, solid cinematic art, one clear open path into the Theatre world.

import QtQuick
import "TheatreApi.js" as TheatreApi

Glass {
    id: marquee

    property var featured: []
    property var continueItems: []
    property int posterW: 114
    property int posterH: 168
    readonly property var hero: featured.length > 0 ? featured[0] : ({})

    signal clicked()
    signal movieClicked(int index)

    radius: 18
    height: 430
    clip: true

    Theme { id: theme }

    Component.onCompleted: TheatreApi.loadHome(function(rows) {
        if (rows.featured.length > 0)
            marquee.featured = rows.featured
        if (rows.continueItems.length > 0)
            marquee.continueItems = rows.continueItems
    })

    Rectangle {
        anchors.fill: parent
        radius: marquee.radius
        clip: true
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: marquee.hero.c2 !== undefined ? marquee.hero.c2 : "#0c1118" }
            GradientStop { position: 0.45; color: marquee.hero.c1 !== undefined ? marquee.hero.c1 : "#33445d" }
            GradientStop { position: 1.0; color: "#080a0e" }
        }

        Image {
            anchors.fill: parent
            anchors.leftMargin: parent.width * 0.34
            source: marquee.hero.art !== undefined ? marquee.hero.art : ""
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            opacity: status === Image.Ready ? 0.72 : 0
            Behavior on opacity { NumberAnimation { duration: 240 } }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#ee05060a" }
                GradientStop { position: 0.45; color: "#b805060a" }
                GradientStop { position: 1.0; color: "#3305060a" }
            }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.45
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "#0005060a" }
                GradientStop { position: 1.0; color: "#cc05060a" }
            }
        }
    }

    MouseArea {
        id: panelMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: marquee.clicked()
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 42
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -22
        width: Math.min(520, parent.width * 0.42)
        spacing: 12

        Text {
            text: "THEATRE"
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 11
            font.letterSpacing: 3
        }
        Text {
            text: marquee.hero.title !== undefined ? marquee.hero.title : "Theatre"
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 46
            wrapMode: Text.WordWrap
            maximumLineCount: 2
        }
        Text {
            text: marquee.hero.blurb !== undefined ? marquee.hero.blurb : "Movies, shows, addons, and continue watching."
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
            lineHeight: 1.25
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            width: parent.width
        }
        Rectangle {
            radius: 11
            height: 42
            width: cta.implicitWidth + 36
            color: panelMouse.containsMouse ? Qt.lighter(theme.gold, 1.08) : theme.gold
            Text {
                id: cta
                anchors.centerIn: parent
                text: "Open Theatre"
                color: "#1a1408"
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 38
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 34
        spacing: 16

        Repeater {
            model: Math.min(5, marquee.continueItems.length)
            delegate: Item {
                id: card
                required property int index
                readonly property var itemData: marquee.continueItems[index]
                width: marquee.posterW
                height: marquee.posterH + 28
                y: posterMouse.containsMouse ? -8 : 0
                Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                Rectangle {
                    width: marquee.posterW
                    height: marquee.posterH
                    radius: 10
                    color: card.itemData.c2 !== undefined ? card.itemData.c2 : "#10151b"
                    border.width: 1
                    border.color: posterMouse.containsMouse ? theme.gold : Qt.rgba(1, 1, 1, 0.14)
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: card.itemData.cover !== undefined ? card.itemData.cover : ""
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 228
                        sourceSize.height: 336
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 4
                        color: Qt.rgba(1, 1, 1, 0.22)
                        Rectangle {
                            width: parent.width * (card.itemData.progress !== undefined ? card.itemData.progress : 0)
                            height: parent.height
                            color: theme.gold
                        }
                    }
                }
                Text {
                    anchors.top: parent.top
                    anchors.topMargin: marquee.posterH + 9
                    width: marquee.posterW
                    text: card.itemData.caption !== undefined ? card.itemData.caption : ""
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: posterMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: marquee.movieClicked(card.index)
                }
            }
        }
    }
}
