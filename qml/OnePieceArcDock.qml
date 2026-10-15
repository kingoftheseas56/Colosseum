pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    required property var arc
    required property Item backdrop

    signal animeRequested()
    signal onePaceRequested()
    signal mangaRequested(bool colorEdition)
    signal liveActionRequested()
    signal specialRequested(string id)

    Theme { id: theme }

    function displayRange(value) {
        return String(value || "").replace(/-/g, "\u2013")
    }

    function mediaForArc() {
        var rows = [
            { action: "anime", kind: "THEATRE", title: "Anime",
              detail: "Original Anime", cta: "WATCH",
              art: "https://live.metahub.space/background/medium/tt0388629/img" },
            { action: "pace", kind: "ONE PACE · STREMIO", title: "One Pace",
              detail: "One Pace via Stremio", cta: "OPEN",
              art: "https://live.metahub.space/background/medium/tt0388629/img" },
            { action: "manga", kind: "TANKOBAN", title: "Manga",
              detail: "Original Manga", cta: "READ",
              art: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg" },
            { action: "color", kind: "TANKOBAN · COLOR", title: "Colored Manga",
              detail: "Colored Manga", cta: "READ",
              art: "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg" },
            { action: "live", id: "tt11737520", kind: "LIVE ACTION", title: "Live Action",
              detail: "Netflix Live Action", cta: "WATCH",
              art: "https://live.metahub.space/background/medium/tt11737520/img" },
            { action: "special", id: "tt11757066", kind: "SPECIAL", title: "Episode of East Blue",
              detail: "East Blue Special", cta: "WATCH",
              art: "https://live.metahub.space/background/medium/tt11757066/img" }
        ]
        if (root.arc.id === "arlong") {
            rows.push({ action: "special", id: "tt2598466", kind: "SPECIAL", title: "Episode of Nami",
                        detail: "Arlong Park Special", cta: "WATCH",
                        art: "https://live.metahub.space/background/medium/tt2598466/img" })
        }
        return rows
    }

    readonly property var routeModel: mediaForArc()

    function activate(row) {
        if (!row) return
        if (row.action === "anime") root.animeRequested()
        else if (row.action === "pace") root.onePaceRequested()
        else if (row.action === "manga") root.mangaRequested(false)
        else if (row.action === "color") root.mangaRequested(true)
        else if (row.action === "live") root.liveActionRequested()
        else if (row.action === "special") root.specialRequested(row.id)
    }

    function restartAutoAdvance() {
        autoAdvance.stop()
        if (root.routeModel.length > 1) autoAdvance.start()
    }

    function showIndex(index, resetTimer) {
        var count = root.routeModel.length
        if (!count) return
        mediaCarousel.index = (index % count + count) % count
        if (resetTimer) root.restartAutoAdvance()
    }

    onArcChanged: Qt.callLater(function() {
        mediaCarousel.index = 0
        root.restartAutoAdvance()
    })

    Timer {
        id: autoAdvance
        interval: 5000
        repeat: true
        running: root.visible && root.routeModel.length > 1
        onTriggered: root.showIndex(mediaCarousel.index + 1, false)
    }

    Glass {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: 18
        tint: 0.045
        scrim: 0.28
        blurAmount: 0.68

        Item {
            anchors.fill: parent
            anchors.margins: 20

            Item {
                id: metaHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 112

                Column {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    width: parent.width * 0.68
                    spacing: 3

                    Text { text: "EAST BLUE SAGA"; color: theme.gold; font.family: theme.ui; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2.7 }
                    Text { text: root.arc.title + " Arc"; color: theme.ink; font.family: theme.display; font.pixelSize: 29; elide: Text.ElideRight; width: parent.width }
                    Text { text: root.arc.place; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                    Text {
                        text: root.arc.summary || ""
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 10
                        lineHeight: 1.16
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: 28
                    Column { spacing: 2; Text { text: root.displayRange(root.arc.anime); color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.bold: true } Text { text: root.arc.episodeCount + " episodes"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 9; font.capitalization: Font.AllUppercase } }
                    Column { spacing: 2; Text { text: root.displayRange(root.arc.chapters); color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.bold: true } Text { text: root.arc.chapterCount + " chapters"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 9; font.capitalization: Font.AllUppercase } }
                    Column { spacing: 2; Text { text: root.displayRange(root.arc.volumes); color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.bold: true } Text { text: "manga volumes"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 9; font.capitalization: Font.AllUppercase } }
                }
            }

            Item {
                id: carouselHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: metaHeader.bottom
                height: 30

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "EXPLORE THIS ARC"
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.5
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.routeModel.length ? (mediaCarousel.index + 1) + " / " + root.routeModel.length : ""
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 10
                    font.letterSpacing: 1.0
                }
            }

            FeaturedCarousel {
                id: mediaCarousel
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: carouselHeader.bottom
                anchors.topMargin: 7
                anchors.bottom: parent.bottom
                slides: root.routeModel
                kicker: ""
                primaryLabel: "OPEN"
                secondaryLabel: ""
                onPrimaryClicked: function(index) {
                    root.restartAutoAdvance()
                    root.activate(root.routeModel[index])
                }
                onIndexChanged: root.restartAutoAdvance()
            }

        }
    }
}
