import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    property var detailData: ({})
    property bool inWatchlist: false
    property bool favorite: false
    property bool watched: false
    property alias contentY: flick.contentY

    signal playRequested(var item)
    signal downloadRequested(var item)
    signal itemRequested(var item)
    signal episodeDetailsRequested(var episode)

    Theme { id: theme }

    function initials(name) {
        var parts = String(name || "").trim().split(/\s+/)
        if (!parts.length) return "?"
        if (parts.length === 1) return parts[0].charAt(0).toUpperCase()
        return String(parts[0].charAt(0) + parts[parts.length - 1].charAt(0)).toUpperCase()
    }

    Rectangle {
        anchors.fill: parent
        color: "#06070b"
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }

        Column {
            id: pageColumn
            width: flick.width
            spacing: 0

            HarborHero {
                id: hero
                width: parent.width
                viewportHeight: root.height
                detailData: root.detailData
                inWatchlist: root.inWatchlist
                favorite: root.favorite
                watched: root.watched

                onPlayRequested: root.playRequested(root.detailData)
                onWatchlistRequested: root.inWatchlist = !root.inWatchlist
                onFavoriteRequested: root.favorite = !root.favorite
                onListRequested: console.log("[arc56] list action", root.detailData.title)
                onWatchedRequested: root.watched = !root.watched
                onTrailerRequested: console.log("[arc56] trailer action", root.detailData.title)
                onDownloadRequested: root.downloadRequested(root.detailData)
            }

            Item {
                width: parent.width
                height: detailColumn.implicitHeight + 152

                Column {
                    id: detailColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 48
                    anchors.rightMargin: 48
                    anchors.top: parent.top
                    anchors.topMargin: 56
                    spacing: 64

                    Text {
                        width: Math.min(900, parent.width)
                        text: root.detailData.synopsis || ""
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 15
                        lineHeight: 1.55
                        wrapMode: Text.WordWrap
                    }

                    Column {
                        visible: (root.detailData.providers || []).length > 0
                        width: parent.width
                        spacing: 12

                        Text {
                            text: "WATCH ON"
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            font.letterSpacing: 2.2
                        }

                        Flow {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: root.detailData.providers || []

                                delegate: Rectangle {
                                    required property var modelData
                                    height: 44
                                    width: providerRow.implicitWidth + 22
                                    radius: 12
                                    color: providerAction.interactionActive
                                           ? Qt.rgba(1, 1, 1, 0.10)
                                           : Qt.rgba(1, 1, 1, 0.055)
                                    border.width: 1
                                    border.color: providerAction.interactionActive
                                                  ? theme.inkDimmer : Qt.rgba(1, 1, 1, 0.10)

                                    Row {
                                        id: providerRow
                                        anchors.centerIn: parent
                                        spacing: 9

                                        Rectangle {
                                            width: 28
                                            height: 28
                                            radius: 7
                                            color: Qt.rgba(1, 1, 1, 0.10)

                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.mark || String(modelData.name || "").slice(0, 2)
                                                color: theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: 9
                                                font.weight: Font.Bold
                                            }
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name || ""
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    KeyboardAction {
                                        id: providerAction
                                        anchors.fill: parent
                                        accessibleName: "Watch on " + (modelData.name || "")
                                        focusRadius: parent.radius
                                        onTriggered: console.log("[arc56] provider", modelData.name)
                                    }
                                }
                            }
                        }
                    }

                    HarborEpisodeBrowser {
                        visible: root.detailData.type === "series"
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        seasons: root.detailData.seasons || []
                        episodes: root.detailData.episodes || []
                        currentSeason: 1
                        onPlayRequested: episode => root.playRequested(episode)
                        onDetailsRequested: episode => root.episodeDetailsRequested(episode)
                        onDownloadRequested: episode => root.downloadRequested(episode)
                    }

                    Column {
                        visible: (root.detailData.credits || []).length > 0
                        width: parent.width
                        spacing: 24

                        Text {
                            text: "Crew"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 22
                            font.weight: Font.Medium
                        }

                        Flow {
                            width: parent.width
                            spacing: 28

                            Repeater {
                                model: root.detailData.credits || []

                                delegate: Column {
                                    required property var modelData
                                    width: Math.max(230, (detailColumn.width - 96) / 3)
                                    spacing: 6

                                    Text {
                                        text: String(modelData.label || "").toUpperCase()
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        font.letterSpacing: 2.1
                                    }

                                    Text {
                                        width: parent.width
                                        text: modelData.value || ""
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 15
                                        lineHeight: 1.25
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Qt.rgba(1, 1, 1, 0.08)
                        }
                    }

                    Column {
                        visible: (root.detailData.cast || []).length > 0
                        width: parent.width
                        spacing: 20

                        Text {
                            text: "Cast · " + (root.detailData.cast || []).length
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 22
                            font.weight: Font.Medium
                        }

                        Flickable {
                            width: parent.width
                            height: 222
                            contentWidth: castRow.implicitWidth
                            contentHeight: height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            Row {
                                id: castRow
                                spacing: 16

                                Repeater {
                                    model: root.detailData.cast || []

                                    delegate: Item {
                                        id: castCard
                                        required property var modelData
                                        width: 128
                                        height: 216

                                        Rectangle {
                                            id: castFace
                                            width: 128
                                            height: 160
                                            radius: 12
                                            color: Qt.rgba(1, 1, 1, 0.06)
                                            border.width: castAction.activeFocus ? 2 : 0
                                            border.color: theme.gold
                                            y: castAction.hovered ? -6 : 0

                                            Behavior on y {
                                                NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                text: root.initials(castCard.modelData.name)
                                                color: theme.inkDimmer
                                                font.family: theme.display
                                                font.pixelSize: 34
                                            }
                                        }

                                        Text {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: castFace.bottom
                                            anchors.topMargin: 9
                                            text: castCard.modelData.name || ""
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            text: castCard.modelData.role || ""
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 12
                                            maximumLineCount: 2
                                            wrapMode: Text.WordWrap
                                            elide: Text.ElideRight
                                        }

                                        KeyboardAction {
                                            id: castAction
                                            anchors.fill: parent
                                            accessibleName: castCard.modelData.name || "Cast"
                                            focusRadius: 12
                                            onTriggered: console.log("[arc56] cast", castCard.modelData.name)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    HarborPosterRail {
                        visible: (root.detailData.collection || []).length > 0
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        title: "Collection"
                        items: root.detailData.collection || []
                        onItemRequested: item => root.itemRequested(item)
                    }

                    HarborPosterRail {
                        visible: (root.detailData.moreLike || []).length > 0
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        title: "More Like This"
                        items: root.detailData.moreLike || []
                        onItemRequested: item => root.itemRequested(item)
                    }

                    HarborPosterRail {
                        visible: (root.detailData.similar || []).length > 0
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        title: "You Might Also Like"
                        items: root.detailData.similar || []
                        onItemRequested: item => root.itemRequested(item)
                    }

                    Column {
                        visible: (root.detailData.media || []).length > 0
                        width: parent.width
                        spacing: 20

                        Text {
                            text: "Media"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 22
                            font.weight: Font.Medium
                        }

                        Row {
                            width: parent.width
                            height: 220
                            spacing: 12

                            Repeater {
                                model: root.detailData.media || []

                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: index === 0 ? (parent.width - 24) * 0.5
                                                       : (parent.width - 24) * 0.25
                                    height: parent.height
                                    radius: 14
                                    color: Qt.rgba(1, 1, 1, 0.06)
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        source: modelData
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        visible: String(root.detailData.awards || "").length > 0
                        width: parent.width
                        spacing: 18

                        Text {
                            text: "Awards & Recognition"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 22
                            font.weight: Font.Medium
                        }

                        Rectangle {
                            width: parent.width
                            height: 86
                            radius: 16
                            color: Qt.rgba(1, 1, 1, 0.035)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)

                            Text {
                                anchors.fill: parent
                                anchors.margins: 20
                                text: root.detailData.awards || ""
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 14
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Column {
                        visible: (root.detailData.info || []).length > 0
                        width: parent.width
                        spacing: 24

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Qt.rgba(1, 1, 1, 0.08)
                        }

                        Text {
                            text: "Information"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 22
                            font.weight: Font.Medium
                        }

                        Flow {
                            width: parent.width
                            spacing: 28

                            Repeater {
                                model: root.detailData.info || []

                                delegate: Column {
                                    required property var modelData
                                    width: Math.max(230, (detailColumn.width - 96) / 3)
                                    spacing: 6

                                    Text {
                                        text: String(modelData.label || "").toUpperCase()
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        font.letterSpacing: 2.1
                                    }

                                    Text {
                                        width: parent.width
                                        text: modelData.value || ""
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 14
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
