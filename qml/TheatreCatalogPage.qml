// TheatreCatalogPage - one Theatre tab page: a ranked Top 10 row + the genre boxes
// (Tankoban-manga GenreMosaic treatment). MAL-template genre pages open behind the tiles.

import QtQuick
import "TheatreApi.js" as TheatreApi
import "TheatreGenreApi.js" as TheatreGenreApi

pragma ComponentBehavior: Bound

Column {
    id: page

    property string pageKey: "movies"
    property var rows: []
    property bool loading: false
    property string errorText: ""
    signal itemRequested(var item)
    signal genreRequested(string kind, string name)
    signal genreIndexRequested(string kind)
    signal discoverPinRequested(var pin)
    signal seeAllRequested(var pin)

    width: parent ? parent.width : 900
    spacing: 30

    Theme { id: theme }

    // tab key → the API's media kind
    readonly property string mediaKind: pageKey === "movies" ? "movie"
                                       : pageKey === "shows" ? "series" : "anime"
    readonly property string genreBoxTitle: pageKey === "movies" ? "Movie Genres"
                                           : pageKey === "shows" ? "Show Genres" : "Anime Genres"
    // mosaic model + cover pool (tiles cycle the Top 10 posters until per-genre bakes land)
    readonly property var genreTiles: TheatreGenreApi.mosaicGenres(mediaKind)
    readonly property var coverPool: {
        var out = []
        for (var i = 0; i < rows.length; i++) {
            var items = rows[i].items || []
            for (var j = 0; j < items.length; j++)
                if (items[j].cover) out.push(items[j].cover)
        }
        return out
    }

    onPageKeyChanged: load()
    Component.onCompleted: load()

    function load() {
        loading = true
        errorText = ""
        rows = []
        TheatreApi.loadCatalogPage(pageKey, function(result) {
            if (page.pageKey !== result.pageKey)
                return
            page.loading = false
            page.rows = result.rows || []
            page.errorText = result.error || ""
        })
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
            sourceKind: modelData.sourceKind !== undefined ? modelData.sourceKind : "house"
            sourceLabel: modelData.sourceLabel !== undefined ? modelData.sourceLabel : ""
            seeAllPin: modelData.seeAllPin !== undefined ? modelData.seeAllPin : null
            onItemRequested: (item) => page.itemRequested(item)
            onSeeAllRequested: (pin) => page.seeAllRequested(pin)
        }
    }

    GenreMosaic {
        width: parent.width
        title: page.genreBoxTitle
        genres: page.genreTiles
        covers: page.coverPool
        onGenreClicked: (i) => page.genreRequested(page.mediaKind, page.genreTiles[i].name)
        onExploreClicked: page.genreIndexRequested(page.mediaKind)
    }
}
