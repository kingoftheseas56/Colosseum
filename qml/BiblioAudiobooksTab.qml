// BiblioAudiobooksTab - the retained Audiobooks medium tab in Biblio.
// Two sub-tabs (the house WorldTabBar): Discover (static catalogue rows + genre mosaic) and
// Your Library (downloaded audiobooks with listening progress). It only emits signals; the
// audiobook page, sources page and player are wired by the host.
import QtQuick
import "AudiobookShelfApi.js" as ShelfApi

Column {
    id: tab

    property Flickable pageFlick: null
    property Item backdrop: null
    // True while Biblio shows this tab; the first activation loads the catalogue rows.
    property bool active: false
    // Native services (context properties). Tests inject fakes; absent services are handled.
    property var catalog: (typeof AudiobookCatalog !== "undefined") ? AudiobookCatalog : null
    property var downloads: (typeof Audiobooks !== "undefined") ? Audiobooks : null
    property var progressStore: (typeof Progress !== "undefined") ? Progress : null

    property string subTab: "discover"
    readonly property bool catalogAvailable: tab._available
    readonly property var shelfRows: tab._rows
    readonly property var genreTiles: tab._genres
    readonly property var libraryItems: tab._library

    property bool _available: false
    property var _rows: []
    property var _genres: []
    property var _library: []
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
    function refreshLibrary() {
        tab._library = ShelfApi.libraryCards(
            tab.downloads && tab.downloads.downloadedAudiobooks ? tab.downloads.downloadedAudiobooks() : [],
            tab.progressStore)
    }
    function _activate() {
        if (!tab.active) return
        if (tab._dirty) tab.reload()
        tab.refreshLibrary()
    }
    onActiveChanged: tab._activate()
    onCatalogChanged: tab.invalidateCatalogue()
    onSubTabChanged: if (tab.subTab === "library") tab.refreshLibrary()
    Component.onCompleted: tab._activate()

    Connections {
        target: tab.catalog && tab.catalog.revisionChanged !== undefined ? tab.catalog : null
        ignoreUnknownSignals: true
        function onRevisionChanged() { tab.invalidateCatalogue() }
        function onReadyChanged() { tab.invalidateCatalogue() }
    }

    Connections {
        target: (tab.progressStore && tab.progressStore.revisionChanged !== undefined) ? tab.progressStore : null
        ignoreUnknownSignals: true
        function onRevisionChanged() { if (tab.active) tab.refreshLibrary() }
    }

    width: parent ? parent.width : 900
    spacing: 36
    Theme { id: theme }

    WorldTabBar {
        id: subTabs
        objectName: "audiobooksSubTabBar"
        tabPrefix: "audiobooksSubTab"
        backdrop: tab.backdrop
        currentTab: tab.subTab
        tabModel: [ { key: "discover", label: "Discover" },
                    { key: "library", label: "Your Library" } ]
        onTabRequested: (key) => tab.subTab = key
    }

    // ── Discover ──
    Text {
        objectName: "audiobooksUnavailable"
        visible: tab.subTab === "discover" && tab._loaded && !tab._available
        text: "The audiobook catalogue is unavailable right now."
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: theme.fontSize(14)
    }

    Repeater {
        id: rowRepeater
        model: tab.subTab === "discover" ? tab._rows : []
        delegate: PosterRail {
            id: rail
            required property var modelData
            required property int index
            objectName: "audiobooksRow_" + String(modelData.key).replace(/[^A-Za-z0-9_]/g, "_")
            width: tab.width
            title: modelData.title
            items: modelData.items
            seeAllPin: ({ "key": modelData.key })
            onItemRequested: (item) => tab.audiobookRequested(item.raw)
            onSeeAllRequested: (pin) => tab.audiobookShelfRequested(pin.key)
        }
    }

    GenreMosaic {
        id: genreRow
        objectName: "audiobooksGenreMosaic"
        visible: tab.subTab === "discover" && tab._genres.length > 0
        title: "Explore Audiobooks"
        genres: tab._genres
        navigable: false
        onGenreClicked: (i) => {
            var g = tab._genres[i]
            if (g) tab.audiobookGenreRequested(g.name)
        }
    }

    // ── Your Library ──
    Text {
        objectName: "audiobooksLibraryEmpty"
        visible: tab.subTab === "library" && tab._library.length === 0
        text: "Audiobooks you download will appear here."
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: theme.fontSize(14)
    }

    Flow {
        id: libraryFlow
        objectName: "audiobooksLibraryGrid"
        visible: tab.subTab === "library" && tab._library.length > 0
        width: tab.width
        spacing: 18
        Repeater {
            model: tab.subTab === "library" ? tab._library : []
            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                objectName: "audiobooksLibraryTile_" + String(modelData.id || index).replace(/[^A-Za-z0-9_]/g, "_")
                width: 132
                height: 260
                CataloguePosterCard {
                    anchors.fill: parent
                    item: tile.modelData
                    showAuthorAtRest: true
                    onActivated: (it) => tab.audiobookRequested(it.raw)
                }
                Rectangle {
                    objectName: "audiobooksLibraryProgress"
                    visible: tile.modelData.progress !== undefined
                    x: 0; y: 192; height: 4; radius: 2
                    width: 132 * (tile.modelData.progress !== undefined ? tile.modelData.progress : 0)
                    color: theme.gold
                }
                Text {
                    objectName: "abLibraryState_" + tile.modelData.id
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    text: tile.modelData.listeningState === "finished" ? "Finished"
                          : tile.modelData.listeningState === "started" ? "Started" : "New"
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: theme.fontSize(12)
                }
            }
        }
    }
}
