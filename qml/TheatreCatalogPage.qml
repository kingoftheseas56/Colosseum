// TheatreCatalogPage - one of Harbor's four Theatre catalog pages inside the tab shell.

import QtQuick
import "TheatreApi.js" as TheatreApi

pragma ComponentBehavior: Bound

Column {
    id: page

    property string pageKey: "discover"
    property var rows: []
    property var heroItems: []
    property var topPicks: []
    property var genreTiles: []
    property var languageTiles: []
    property var awardTiles: []
    property bool loading: false
    property string errorText: ""
    signal itemRequested(var item)

    width: parent ? parent.width : 900
    spacing: 30

    Theme { id: theme }

    onPageKeyChanged: load()
    Component.onCompleted: load()

    function load() {
        loading = true
        errorText = ""
        rows = []
        heroItems = []
        topPicks = []
        genreTiles = []
        languageTiles = []
        awardTiles = []
        TheatreApi.loadCatalogPage(pageKey, function(result) {
            if (page.pageKey !== result.pageKey)
                return
            page.loading = false
            page.rows = result.rows || []
            page.heroItems = result.hero || []
            page.topPicks = result.topPicks || []
            page.genreTiles = result.genreTiles || []
            page.languageTiles = result.languageTiles || []
            page.awardTiles = result.awardTiles || []
            page.errorText = result.error || ""
        })
    }

    TheatreCinemaHero {
        visible: page.pageKey === "movies" && page.heroItems.length > 0
        height: visible ? 520 : 0
        slides: page.heroItems
        eyebrow: "Featured tonight"
        onPrimaryClicked: (item) => page.itemRequested(item)
        onSecondaryClicked: (item) => page.itemRequested(item)
    }

    TheatrePeekHero {
        visible: page.pageKey === "shows" && page.heroItems.length > 0
        height: visible ? 455 : 0
        slides: page.heroItems
        onItemRequested: (item) => page.itemRequested(item)
    }

    Column {
        visible: page.pageKey === "anime" && page.heroItems.length > 0
        width: parent.width
        spacing: 24
        height: visible ? implicitHeight : 0

        TheatreCinemaHero {
            width: parent.width
            height: 520
            slides: page.heroItems
            eyebrow: "Anime"
            primaryLabel: "Start Watching"
            secondaryLabel: "Details"
            onPrimaryClicked: (item) => page.itemRequested(item)
            onSecondaryClicked: (item) => page.itemRequested(item)
        }
        PosterRail {
            width: parent.width
            title: "Top Picks for You"
            items: page.topPicks
            itemLimit: 18
            onItemRequested: (item) => page.itemRequested(item)
        }
    }

    Column {
        visible: page.pageKey === "discover" && page.heroItems.length > 0
        width: parent.width
        spacing: 32
        height: visible ? implicitHeight : 0

        TheatreCinemaHero {
            width: parent.width
            height: 430
            slides: page.heroItems
            eyebrow: "Featured & Recommended"
            primaryLabel: "Explore"
            secondaryLabel: "Details"
            onPrimaryClicked: (item) => page.itemRequested(item)
            onSecondaryClicked: (item) => page.itemRequested(item)
        }

        TheatreDiscoveryTiles {
            width: parent.width
            title: "Browse by mood"
            tiles: page.genreTiles
            onTileClicked: (tile) => {
                if (tile.item !== undefined)
                    page.itemRequested(tile.item)
            }
        }

        PosterRail {
            visible: page.awardTiles.length > 0
            width: parent.width
            title: "Award Season Energy"
            items: page.awardTiles
            itemLimit: 8
            onItemRequested: (item) => page.itemRequested(item)
        }

        TheatreDiscoveryTiles {
            width: parent.width
            title: "World cinema"
            tiles: page.languageTiles
            onTileClicked: (tile) => {
                if (tile.item !== undefined)
                    page.itemRequested(tile.item)
            }
        }
    }

    Text {
        visible: !page.loading && page.heroItems.length === 0
                 && (page.pageKey === "movies" || page.pageKey === "shows" || page.pageKey === "anime")
        text: TheatreApi.pageTitle(page.pageKey)
        color: theme.ink
        font.family: theme.display
        font.pixelSize: 32
        font.weight: Font.DemiBold
    }

    Item {
        visible: page.loading
        width: parent.width
        height: 236

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18
            Repeater {
                model: 7
                Rectangle {
                    width: 132
                    height: 196
                    radius: 12
                    color: Qt.rgba(1, 1, 1, 0.08)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.10)
                    opacity: 0.65
                }
            }
        }
    }

    Text {
        visible: !page.loading && page.rows.length === 0
        text: page.errorText.length ? page.errorText : "Nothing loaded here yet."
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 14
    }

    Repeater {
        model: page.rows
        delegate: PosterRail {
            required property var modelData
            width: page.width
            title: modelData.title
            ranked: modelData.ranked === true
            items: modelData.items !== undefined ? modelData.items : []
            onItemRequested: (item) => page.itemRequested(item)
        }
    }
}
