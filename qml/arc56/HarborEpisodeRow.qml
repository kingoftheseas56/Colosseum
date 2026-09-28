import QtQuick
import ".."

Item {
    id: row
    property var episode: ({})
    property bool active: false
    property bool showStats: true

    signal playRequested(var episode)
    signal detailsRequested(var episode)
    signal downloadRequested(var episode)

    implicitHeight: 184
    height: implicitHeight
    HarborTheme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: mainAction.interactionActive ? Qt.rgba(1, 1, 1, 0.035) : "transparent"
        Behavior on color { ColorAnimation { duration: 180 } }
    }

    Item {
        id: mainArea
        anchors.left: parent.left
        anchors.right: actions.left
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Row {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            anchors.topMargin: 20
            anchors.bottomMargin: 20
            spacing: 24

            Item {
                id: artBox
                width: 200
                height: 112.5

                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.06)
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: row.episode.thumbnail || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: status === Image.Ready ? (row.episode.watched ? 0.72 : 1.0) : 0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.40)
                        opacity: mainAction.interactionActive ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        radius: 24
                        color: theme.ink
                        opacity: mainAction.interactionActive ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }

                        HarborIcon {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            iconSize: 18
                            kind: "play"
                            ink: "#17181c"
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 8
                        width: episodeNo.implicitWidth + 12
                        height: 23
                        radius: 6
                        color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.95)
                        Text {
                            id: episodeNo
                            anchors.centerIn: parent
                            text: row.episode.number || 0
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        visible: Number(row.episode.rating || 0) > 0
                        anchors.left: parent.left
                        anchors.bottom: progressTrack.visible ? progressTrack.top : parent.bottom
                        anchors.leftMargin: 8
                        anchors.bottomMargin: 8
                        height: 24
                        width: scoreRow.implicitWidth + 12
                        radius: 6
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Row {
                            id: scoreRow
                            anchors.centerIn: parent
                            spacing: 5
                            Rectangle {
                                width: 30
                                height: 16
                                radius: 3
                                color: "#f5c518"
                                Text {
                                    anchors.centerIn: parent
                                    text: "IMDb"
                                    color: "#111111"
                                    font.family: theme.ui
                                    font.pixelSize: 8
                                    font.weight: Font.Black
                                }
                            }
                            Text {
                                text: row.episode.rating || ""
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Rectangle {
                        visible: row.episode.watched === true
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        width: 24
                        height: 24
                        radius: 12
                        color: Qt.rgba(0.25, 0.82, 0.60, 0.22)
                        border.width: 1
                        border.color: Qt.rgba(0.25, 0.82, 0.60, 0.45)
                        HarborIcon {
                            anchors.centerIn: parent
                            width: 12
                            height: 12
                            iconSize: 12
                            kind: "check"
                            ink: "#a9efd6"
                        }
                    }

                    Rectangle {
                        id: progressTrack
                        visible: Number(row.episode.progress || 0) > 0.01
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Rectangle {
                            width: Math.max(2, parent.width * Number(row.episode.progress || 0))
                            height: parent.height
                            color: theme.gold
                        }
                    }
                }
            }

            Column {
                id: copy
                width: Math.max(0, mainArea.width - artBox.width - 24 - 24)
                spacing: 6

                Text {
                    width: parent.width
                    text: row.episode.title || ("Episode " + (row.episode.number || 0))
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: "S" + (row.episode.season || 0) + " E" + (row.episode.number || 0)
                          + (row.episode.runtime ? "  ·  " + row.episode.runtime : "")
                          + (row.episode.airDate ? "  ·  " + row.episode.airDate : "")
                          + (Number(row.episode.progress || 0) > 0.01
                             ? "  ·  " + (row.episode.watched ? "Watched"
                                : Math.round(Number(row.episode.progress) * 100) + "% watched") : "")
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: row.episode.overview || ""
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: row.showStats
                    width: parent.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                HarborEpisodeStats {
                    visible: row.showStats
                    width: parent.width
                    episode: row.episode
                }
            }
        }

        KeyboardAction {
            id: mainAction
            anchors.fill: parent
            accessibleName: "Play " + (row.episode.title || "episode")
            focusRadius: 16
            onTriggered: row.playRequested(row.episode)
        }
    }

    Item {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: 0
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 104

        HarborActionButton {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: 40
            compact: true
            flat: true
            iconKind: "eye"
            label: "Episode details"
            onTriggered: row.detailsRequested(row.episode)
        }

        HarborActionButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 20
            width: 40
            height: 40
            compact: true
            flat: true
            iconKind: "download"
            label: "Download episode"
            opacity: mainAction.interactionActive || activeFocus ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            onTriggered: row.downloadRequested(row.episode)
        }
    }
}
