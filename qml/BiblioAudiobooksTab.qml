// BiblioAudiobooksTab - the retained Audiobooks medium tab in Biblio.
// Catalogue shelves and genre mosaic. The host owns detail and see-all routes.
import QtQuick
import "AudiobookShelfApi.js" as ShelfApi

Column {
    id: tab

    // True while Biblio shows this tab; the first activation loads the catalogue rows.
    property bool active: false
    // Native services (context properties). Tests inject fakes; absent services are handled.
    property var catalog: (typeof AudiobookCatalog !== "undefined") ? AudiobookCatalog : null

    readonly property bool catalogAvailable: tab._available
    readonly property var shelfRows: tab._rows
    readonly property var genreTiles: tab._genres

    property bool _available: false
    property var _rows: []
    property var _genres: []
    property bool _loaded: false
    property bool _dirty: true

    signal audiobookRequested(var item)
    signal audiobookGenreRequested(string genre)
    signal audiobookShelfRequested(string key)

    function reload() {
        const r = ShelfApi.loadRows(tab.catalog)
        const genres = r.available ? ShelfApi.loadGenres(tab.catalog) : []
        tab._available = r.available
        // Detail and see-all queries also bump the revision. Preserve the
        // landing delegates and their scroll positions if their data is equal.
        if (JSON.stringify(tab._rows) !== JSON.stringify(r.rows)) tab._rows = r.rows
        if (JSON.stringify(tab._genres) !== JSON.stringify(genres)) tab._genres = genres
        tab._loaded = true
        tab._dirty = false
    }
    function invalidateCatalogue() {
        tab._dirty = true
        if (tab.active) tab.reload()
    }
    onActiveChanged: if (tab.active && tab._dirty) tab.reload()
    onCatalogChanged: tab.invalidateCatalogue()
    Component.onCompleted: if (tab.active && tab._dirty) tab.reload()

    Connections {
        target: tab.catalog && tab.catalog.revisionChanged !== undefined ? tab.catalog : null
        ignoreUnknownSignals: true
        function onRevisionChanged() { tab.invalidateCatalogue() }
        function onReadyChanged() { tab.invalidateCatalogue() }
    }

    width: parent ? parent.width : 900
    spacing: 36
    Theme { id: theme }

    Text {
        objectName: "audiobooksUnavailable"
        visible: tab._loaded && !tab._available
        text: "The audiobook catalogue is unavailable right now."
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: theme.fontSize(14)
    }

    Repeater {
        id: rowRepeater
        model: tab._rows
        delegate: PosterRail {
            id: rail
            required property var modelData
            required property int index
            objectName: "audiobooksRow_" + String(modelData.key).replace(/[^A-Za-z0-9_]/g, "_")
            width: tab.width
            title: modelData.title
            items: modelData.items
            squareArtwork: true
            seeAllPin: ({ "key": modelData.key })
            onItemRequested: (item) => tab.audiobookRequested(item.raw)
            onSeeAllRequested: (pin) => tab.audiobookShelfRequested(pin.key)
        }
    }

    GenreMosaic {
        id: genreRow
        objectName: "audiobooksGenreMosaic"
        visible: tab._genres.length > 0
        title: "Explore Audiobooks"
        genres: tab._genres
        navigable: false
        onGenreClicked: (i) => {
            var g = tab._genres[i]
            if (g) tab.audiobookGenreRequested(g.key)
        }
    }
}
