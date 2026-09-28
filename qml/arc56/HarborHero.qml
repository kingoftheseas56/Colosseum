pragma ComponentBehavior: Bound
import QtQuick
import ".."

Item {
    id: hero
    property var detailData: ({})
    property real viewportHeight: 900
    property bool inWatchlist: false
    property bool favorite: false
    property bool watched: false

    signal playRequested()
    signal watchlistRequested()
    signal favoriteRequested()
    signal listRequested()
    signal watchedRequested()
    signal trailerRequested()
    signal downloadRequested()

    implicitHeight: Math.max(640, viewportHeight * 0.78)
    height: implicitHeight

    HarborTheme { id: theme }

    Image {
        id: backdrop
        anchors.fill: parent
        source: hero.detailData.backdrop || hero.detailData.poster || ""
        sourceSize.width: 1920
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignVCenter
        asynchronous: true
        cache: true
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 700 } }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.86) }
            GradientStop { position: 0.52; color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.35) }
            GradientStop { position: 1.0; color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.0) }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.0) }
            GradientStop { position: 0.45; color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.55) }
            GradientStop { position: 1.0; color: "#111213" }
        }
    }

    HarborHeroAwardsCorner {
        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 138
        width: 300
        award: hero.detailData.heroAward || ({})
        visible: hero.width >= 900 && String((hero.detailData.heroAward || {}).headline || "").length > 0
    }

    Column {
        id: copy
        anchors.left: parent.left
        anchors.leftMargin: 48
        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 56
        spacing: 0

        Text {
            visible: String(hero.detailData.tagline || "").length > 0
            text: hero.detailData.tagline || ""
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 14
            font.weight: Font.Medium
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 3
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.7)
            bottomPadding: 16
        }

        Item {
            width: Math.min(900, copy.width)
            height: 124

            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: parent.width
                visible: titleLogo.status !== Image.Ready
                text: hero.detailData.title || ""
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 80
                font.weight: Font.Medium
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 54
                wrapMode: Text.NoWrap
                maximumLineCount: 1
                elide: Text.ElideRight
            }

            Image {
                id: titleLogo
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: Math.min(440, implicitWidth)
                height: Math.min(124, implicitHeight)
                source: hero.detailData.logo || ""
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                verticalAlignment: Image.AlignBottom
                asynchronous: true
                cache: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 500 } }
            }
        }

        Flow {
            id: pills
            width: Math.min(920, copy.width)
            spacing: 12
            topPadding: 24

            HarborPill {
                visible: String(hero.detailData.year || "").length > 0
                label: hero.detailData.year || ""
            }

            Rectangle {
                visible: String(hero.detailData.rating || "").length > 0
                implicitWidth: ratingRow.implicitWidth + 20
                implicitHeight: 28
                radius: height / 2
                color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.85)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)

                Row {
                    id: ratingRow
                    anchors.centerIn: parent
                    spacing: 7

                    Rectangle {
                        width: 34
                        height: 19
                        radius: 3
                        color: "#f5c518"
                        Text {
                            anchors.centerIn: parent
                            text: "IMDb"
                            color: "#111111"
                            font.family: theme.ui
                            font.pixelSize: 9
                            font.weight: Font.Black
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: hero.detailData.rating || ""
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                }
            }

            HarborPill {
                visible: String(hero.detailData.runtime || "").length > 0
                label: hero.detailData.runtime || ""
            }

            Repeater {
                model: hero.detailData.genres || []
                delegate: HarborPill {
                    required property var modelData
                    label: String(modelData)
                }
            }
        }

        Flow {
            width: Math.min(1100, copy.width)
            spacing: 12
            topPadding: 36

            HarborActionButton {
                primary: true
                iconKind: "play"
                label: hero.detailData.primaryLabel || "Play"
                onTriggered: hero.playRequested()
            }

            HarborActionButton {
                iconKind: hero.inWatchlist ? "bookmarkCheck" : "bookmark"
                label: hero.inWatchlist ? "In Watchlist" : "Add to Watchlist"
                checked: hero.inWatchlist
                onTriggered: hero.watchlistRequested()
            }

            HarborActionButton {
                compact: true
                iconKind: "star"
                label: "Favorite"
                checked: hero.favorite
                onTriggered: hero.favoriteRequested()
            }

            HarborActionButton {
                compact: true
                iconKind: "bookmark"
                label: "Add to list"
                onTriggered: hero.listRequested()
            }

            HarborActionButton {
                visible: hero.detailData.type === "movie"
                compact: true
                iconKind: "check"
                label: "Mark watched"
                checked: hero.watched
                onTriggered: hero.watchedRequested()
            }

            HarborActionButton {
                compact: true
                iconKind: "eye"
                label: "Watch trailer"
                onTriggered: hero.trailerRequested()
            }

            HarborActionButton {
                visible: hero.detailData.type === "movie"
                compact: true
                iconKind: "download"
                label: "Download"
                onTriggered: hero.downloadRequested()
            }
        }
    }
}
