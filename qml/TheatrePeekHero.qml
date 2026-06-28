// TheatrePeekHero - centered card carousel for Shows.

import QtQuick

pragma ComponentBehavior: Bound

Item {
    id: hero

    property var slides: []
    signal itemRequested(var item)

    width: parent ? parent.width : 900
    height: slides.length > 0 ? 455 : 0
    visible: slides.length > 0
    clip: true

    property int activeIndex: 0

    Theme { id: theme }

    Timer {
        interval: 9500
        running: hero.visible && hero.slides.length > 1
        repeat: true
        onTriggered: hero.activeIndex = (hero.activeIndex + 1) % hero.slides.length
    }

    Repeater {
        model: hero.slides
        delegate: Rectangle {
            id: card
            required property var modelData
            required property int index

            readonly property int wrapped: {
                var raw = index - hero.activeIndex;
                if (raw > hero.slides.length / 2) raw -= hero.slides.length;
                if (raw < -hero.slides.length / 2) raw += hero.slides.length;
                return raw;
            }
            visible: Math.abs(wrapped) <= 1
            width: Math.min(920, hero.width * 0.70)
            height: 420
            radius: 20
            clip: true
            x: hero.width / 2 - width / 2 + wrapped * hero.width * 0.44
            y: 10 + Math.abs(wrapped) * 24
            scale: wrapped === 0 ? 1.0 : 0.88
            opacity: wrapped === 0 ? 1.0 : 0.46
            z: wrapped === 0 ? 4 : 1
            color: card.modelData.c2 !== undefined ? card.modelData.c2 : "#101218"
            border.width: 1
            border.color: wrapped === 0 ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.08)

            Behavior on x { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: 620; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }

            Image {
                anchors.fill: parent
                source: card.modelData.art !== undefined ? card.modelData.art : (card.modelData.cover || "")
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 1000
                sourceSize.height: 560
                opacity: status === Image.Ready ? 0.95 : 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.08) }
                    GradientStop { position: 0.58; color: Qt.rgba(0, 0, 0, 0.24) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.86) }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 34
                spacing: 9
                Text {
                    width: parent.width
                    text: card.modelData.title || ""
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 42
                    font.weight: Font.DemiBold
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap
                }
                Row {
                    visible: card.wrapped === 0
                    spacing: 10
                    Text {
                        text: card.modelData.releaseInfo || ""
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.letterSpacing: 1.8
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        text: "Series"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.letterSpacing: 1.8
                        font.capitalization: Font.AllUppercase
                    }
                }
                Row {
                    visible: card.wrapped === 0
                    spacing: 10
                    Rectangle {
                        width: 92
                        height: 38
                        radius: 19
                        color: theme.ink
                        Text {
                            anchors.centerIn: parent
                            text: "Play"
                            color: "#08090b"
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: hero.itemRequested(card.modelData)
                        }
                    }
                    Rectangle {
                        width: 112
                        height: 38
                        radius: 19
                        color: Qt.rgba(1, 1, 1, 0.12)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.24)
                        Text {
                            anchors.centerIn: parent
                            text: "Episodes"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: hero.itemRequested(card.modelData)
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: card.wrapped !== 0
                cursorShape: Qt.PointingHandCursor
                onClicked: hero.activeIndex = card.index
            }
        }
    }
}
