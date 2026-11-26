// BiblioAudiobooksTab - the retained Audiobooks medium tab in Biblio.
// Catalogue shelves and genre mosaic. The host owns detail and see-all routes.
import QtQuick
import "AudiobookShelfApi.js" as ShelfApi

Column {
    id: tab

    // True while Biblio shows this tab; the first activation loads the catalogue rows.
    property bool active: false
    property Flickable pageFlick: null
    readonly property int artworkPrefetch: 120
    readonly property real artworkBandTop: {
        if (!tab.pageFlick)
            return 0
        // mapToItem does not register position changes with the binding.
        tab.y
        return tab.pageFlick.contentY - tab.mapToItem(tab.pageFlick.contentItem, 0, 0).y - tab.artworkPrefetch
    }
    property bool _artworkLayoutReady: false
    readonly property real artworkBandHeight: tab._artworkLayoutReady && tab.active && tab.pageFlick && tab.pageFlick.height > 0
        ? tab.pageFlick.height + 2 * tab.artworkPrefetch : 0

    function artworkViewportFor(section, bandHeight) {
        return tab.pageFlick
            ? Qt.rect(0, tab.artworkBandTop - section.y, section.width, bandHeight)
            : null
    }

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
    property bool _waiting: false
    property bool _failed: false
    property var _retries: []
    readonly property bool _hasContent: tab._rows.length > 0 || tab._genres.length > 0

    signal audiobookRequested(var item)
    signal audiobookGenreRequested(string genre)
    signal audiobookShelfRequested(string key)

    function settleArtworkLayout() {
        if (!tab.pageFlick)
            return
        // Position the shelf columns and genre grid before exposing their positions to the cover bindings.
        for (let i = 0; i < rowRepeater.count; ++i) {
            const rail = rowRepeater.itemAt(i)
            if (rail)
                rail.forceLayout()
        }
        genreRow.layoutArtworkGrid()
        tab.forceLayout()
        tab._artworkLayoutReady = true
    }

    function reload() {
        const r = ShelfApi.loadLanding(tab.catalog)
        tab._available = r.available
        // Detail and see-all queries also bump the revision. Preserve the
        // landing delegates and their scroll positions if their data is equal.
        const rowsChanged = JSON.stringify(tab._rows) !== JSON.stringify(r.rows)
        const genresChanged = JSON.stringify(tab._genres) !== JSON.stringify(r.genres)
        if (rowsChanged || genresChanged) {
            tab._artworkLayoutReady = false
            if (rowsChanged) tab._rows = r.rows
            if (genresChanged) tab._genres = r.genres
            Qt.callLater(tab.settleArtworkLayout)
        }
        tab._waiting = r.waiting
        tab._failed = r.failed
        tab._retries = r.retries
        tab._loaded = true
        tab._dirty = false
    }
    function invalidateCatalogue() {
        tab._dirty = true
        if (tab.active) tab.reload()
    }
    function retry() {
        const queries = tab._retries
        for (const query of queries)
            tab.catalog.retryRemoteQuery(query.method, query.args)
        tab.reload()
    }
    onActiveChanged: if (tab.active && tab._dirty) tab.reload()
    onCatalogChanged: tab.invalidateCatalogue()
    Component.onCompleted: if (tab.active && tab._dirty) tab.reload()

    Connections {
        target: tab.catalog && tab.catalog.revisionChanged !== undefined ? tab.catalog : null
        ignoreUnknownSignals: true
        function onRevisionChanged() { tab.invalidateCatalogue() }
        function onReadyChanged() { tab.invalidateCatalogue() }
        function onRequestFailed(method) {
            if (method === "shelves" || method === "shelf" || method === "genres")
                tab.invalidateCatalogue()
        }
    }

    width: parent ? parent.width : 900
    spacing: 36
    Theme { id: theme }

    Column {
        width: tab.width
        spacing: 8
        visible: tab._loaded && (tab._failed || !tab._hasContent)

        Text {
            objectName: "audiobooksUnavailable"
            width: parent.width
            wrapMode: Text.WordWrap
            text: {
                if (!tab._available) return qsTr("The audiobook catalogue is unavailable right now.")
                if (tab._failed) return tab._hasContent ? qsTr("Some audiobook shelves could not load.")
                                                        : qsTr("The audiobook catalogue could not load.")
                if (tab._waiting) return qsTr("Loading audiobooks…")
                return qsTr("No audiobooks found.")
            }
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(14)
        }

        ExtensionsActionButton {
            objectName: "audiobooksRetry"
            visible: tab._retries.length > 0
            text: qsTr("Retry")
            onClicked: tab.retry()
        }
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
            visualProfile: "gallery"
            fitArtwork: true
            artworkViewport: tab.artworkViewportFor(rail, tab.artworkBandHeight)
            seeAllPin: ({ "key": modelData.key })
            onItemRequested: (item) => tab.audiobookRequested(item.raw)
            onSeeAllRequested: (pin) => tab.audiobookShelfRequested(pin.key)
        }
    }

    GenreMosaic {
        id: genreRow
        objectName: "audiobooksGenreMosaic"
        visible: tab._genres.length > 0
        title: qsTr("Explore Audiobooks")
        genres: tab._genres
        // Pending shelves can still move the grid down.
        artworkViewport: tab.artworkViewportFor(genreRow, tab._waiting ? 0 : tab.artworkBandHeight)
        navigable: false
        onGenreClicked: (i) => {
            var g = tab._genres[i]
            if (g) tab.audiobookGenreRequested(g.key)
        }
    }
}
