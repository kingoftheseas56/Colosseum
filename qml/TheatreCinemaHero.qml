// TheatreCinemaHero - Harbor-style cinematic page hero for Movies/Theatre.

import QtQuick
import QtQuick.Controls

pragma ComponentBehavior: Bound

Item {
    id: hero

    property var slides: []
    property string eyebrow: "Featured tonight"
    property string primaryLabel: "Watch"
    property string secondaryLabel: "More info"
    signal primaryClicked(var item)
    signal secondaryClicked(var item)
    readonly property bool compactCopy: height < 480

    width: parent ? parent.width : 900
    height: 520
    visible: slides.length > 0

    Theme { id: theme }

    SwipeView {
        id: view
        anchors.fill: parent
        clip: true

        Repeater {
            model: hero.slides
            delegate: Item {
                id: slide
                required property var modelData

                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    clip: true
                    color: slide.modelData.c2 !== undefined ? slide.modelData.c2 : "#07090d"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.14)

                    Image {
                        anchors.fill: parent
                        source: slide.modelData.art !== undefined ? slide.modelData.art : (slide.modelData.cover || "")
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 1280
                        sourceSize.height: 720
                        opacity: status === Image.Ready ? 0.86 : 0
                        Behavior on opacity { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                    }

                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.94) }
                            GradientStop { position: 0.46; color: Qt.rgba(0, 0, 0, 0.48) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.10) }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.08) }
                            GradientStop { position: 0.62; color: Qt.rgba(0, 0, 0, 0.12) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.72) }
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: hero.compactCopy ? 44 : 56
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: hero.compactCopy ? 42 : 58
                        width: Math.min(hero.compactCopy ? 560 : 620, parent.width * 0.48)
                        spacing: hero.compactCopy ? 10 : 14

                        Text {
                            text: hero.eyebrow
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 4.4
                            font.capitalization: Font.AllUppercase
                        }
                        Text {
                            width: parent.width
                            text: slide.modelData.title !== undefined ? slide.modelData.title : ""
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: hero.compactCopy ? 50 : 64
                            font.weight: Font.DemiBold
                            lineHeight: 0.94
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            style: Text.Raised
                            styleColor: Qt.rgba(0, 0, 0, 0.44)
                        }
                        Row {
                            spacing: 12
                            Text {
                                visible: slide.modelData.releaseInfo !== undefined && slide.modelData.releaseInfo.length > 0
                                text: slide.modelData.releaseInfo || ""
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                visible: slide.modelData.type !== undefined
                                text: slide.modelData.type === "series" ? "Series" : "Movie"
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 13
                            }
                        }
                        Text {
                            width: parent.width
                            text: slide.modelData.blurb !== undefined ? slide.modelData.blurb : ""
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: hero.compactCopy ? 14 : 15
                            lineHeight: 1.28
                            wrapMode: Text.WordWrap
                            maximumLineCount: hero.compactCopy ? 2 : 3
                            elide: Text.ElideRight
                        }
                        Row {
                            spacing: 12
                            topPadding: hero.compactCopy ? 4 : 8
                            Rectangle {
                                radius: 10
                                width: primaryText.implicitWidth + 42
                                height: 44
                                color: theme.ink
                                Text {
                                    id: primaryText
                                    anchors.centerIn: parent
                                    text: hero.primaryLabel
                                    color: "#090a0d"
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: hero.primaryClicked(slide.modelData)
                                }
                            }
                            Rectangle {
                                radius: 10
                                width: secondaryText.implicitWidth + 38
                                height: 44
                                color: Qt.rgba(1, 1, 1, 0.10)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.20)
                                Text {
                                    id: secondaryText
                                    anchors.centerIn: parent
                                    text: hero.secondaryLabel
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: hero.secondaryClicked(slide.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Row {
        visible: hero.slides.length > 1
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        spacing: 9
        Repeater {
            model: hero.slides.length
            delegate: Rectangle {
                id: dot
                required property int index
                width: dot.index === view.currentIndex ? 38 : 7
                height: 7
                radius: 4
                color: dot.index === view.currentIndex ? theme.ink : Qt.rgba(1, 1, 1, 0.48)
                Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    cursorShape: Qt.PointingHandCursor
                    onClicked: view.currentIndex = dot.index
                }
            }
        }
    }
}
