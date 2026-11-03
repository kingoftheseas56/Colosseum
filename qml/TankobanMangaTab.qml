// TankobanMangaTab — the Manga half of the Tankoban world's browse (spec 2026-07-18).
// Catalogue-owned manga shelves; static top/genre doors remain available offline.
// Presentation only: canonical identity/source resolution stays in existing owners.
import QtQuick
import "Catalog.js" as Catalog
import "MangaBrowseDelivery.js" as MangaBrowse

Column {
    id: mangaTab
    property Flickable pageFlick: null
    property Item backdrop: null
    property var parkRowHandler: null
    readonly property real contentLeft: rowIndex.contentLeft
    readonly property real _availableWidth: parent ? parent.width : 0
    property var malCatalog: null
    property bool showExplicitContent: false
    property var shelfRows: []
    property bool shelvesLoading: false
    property string shelfError: ""
    property var _cancelShelves: null
    property int _generation: 0
    property bool _ready: false
    property bool _layingOut: false
    property int _shelfRevision: 0
    readonly property var popularShelf: shelfRows.length && shelfRows[0].key === "popular" ? shelfRows[0] : null
    readonly property var additionalShelves: shelfRows.filter(function(row) { return row.key !== "popular" })
    readonly property real viewportTop: {
        if (!pageFlick) return 0
        var track = pageFlick.contentY
        return pageFlick.mapToItem(mangaTab, 0, 0).y
    }
    readonly property var rowIndexRows: {
        var revision = _shelfRevision
        var out = [
            {key:"collection", title:"Your Collection", target:collectionRow},
            {key:"top-manga", title:topMangaRow.title, target:topMangaRow}
        ]
        for (var i = 0; i < shelfRepeater.count; ++i) {
            var shelf = shelfRepeater.itemAt(i)
            if (shelf) out.push({key:shelf.row.key, title:shelf.row.title, target:shelf})
        }
        out.push({key:"genres", title:"Explore by Genre — Manga", target:genreRow})
        return out
    }
    function loadShelves(clear, retry) {
        if (!_ready) return
        if (_cancelShelves) _cancelShelves()
        _cancelShelves = null
        var generation = ++_generation
        if (clear) shelfRows = []
        shelvesLoading = true
        shelfError = ""
        _cancelShelves = MangaBrowse.load(malCatalog, showExplicitContent, function(result) {
            if (generation !== mangaTab._generation) return
            // Keep previously delivered rows on a transient failure. Preference/source
            // changes explicitly clear first, so stale explicit cards cannot leak back.
            if (result.rows.length || !result.error) {
                mangaTab._layingOut = true
                mangaTab.shelfRows = result.rows
                mangaTab.forceLayout()
                mangaTab._layingOut = false
            }
            mangaTab.shelfError = result.error
            mangaTab.shelvesLoading = false
        }, retry === true)
    }
    onMalCatalogChanged: loadShelves(true)
    onShowExplicitContentChanged: loadShelves(true)
    Component.onCompleted: { _ready = true; loadShelves(false) }
    Component.onDestruction: { ++_generation; if (_cancelShelves) _cancelShelves() }
    Connections {
        target: mangaTab.malCatalog
        ignoreUnknownSignals: true
        function onReadyChanged() { mangaTab.loadShelves(true) }
    }
    x: mangaTab.contentLeft
    width: Math.max(0, mangaTab._availableWidth - mangaTab.contentLeft)
    spacing: 36

    RowIndexSidebar {
        id: rowIndex
        parent: mangaTab.parent
        z: 2
        x: 0
        pageFlick: mangaTab.pageFlick
        flowHost: mangaTab
        backdrop: mangaTab.backdrop
        worldName: "Tankoban"
        automationPrefix: "tankobanRowIndex"
        rows: mangaTab.rowIndexRows
        onRowRequested: (target) => { if (mangaTab.parkRowHandler) mangaTab.parkRowHandler(target) }
    }

    signal seriesRequested(string title)
    signal seriesByIdRequested(string title, string malId)
    signal genreRequested(string name)
    signal genreIndexRequested()
    // Bubbles a tap on a Your Collection tile up to the world (manga-filtered).
    signal collectionOpenRequested(var entry)
    // Task 8: a See-all door on a Manga shelf emits a Discover pin. The world switches to
    // Discover and applies it. Pin shape (spec 3.6): {type,catalogId,filterGroup,filterKey}.
    signal discoverPinRequested(var pin)
    signal contentLeftUpdated(real value)
    onContentLeftChanged: mangaTab.contentLeftUpdated(mangaTab.contentLeft)

    ContinueRow {
        id: collectionRow
        title: "Your Collection"
        primaryMangaResumeEnabled: false
        showSeeAll: false
        items: (Collection.revision, Collection.items("tankoban").filter(function(e) { return e.type === "manga" }))
        forgetHandler: function(e) { Collection.remove("tankoban", String(e.id)) }
        onDetailRequested: function(item) { collectionOpenRequested(item) }
        onResumeRequested: function(item) { collectionOpenRequested(item) }
    }

    // "Top in Tankoban — Manga" → Manga / Popular. The header Explore door opens Discover
    // pinned to the Popular manga catalogue. A tile tap still routes to the series page
    // (the existing direct door is unchanged).
    TrendingTop10 {
        id: topMangaRow
        title: mangaTab.popularShelf ? "Popular Manga" : "Top in Tankoban — Manga"
        items: mangaTab.popularShelf ? mangaTab.popularShelf.items : Catalog.topManga
        // World-namespaced automation reach (catalogue-independence Slice 3, 2026-08-20):
        // a Lanista scenario needs a scroll-free click path into a real series page — this
        // rail is reachable without touching the Discover wall's un-named, un-scrollable
        // GridView (the Slice-2 documented bridge gap).
        namePrefix: "tankobanTopMangaTile_"
        onItemClicked: (i) => {
            var item = topMangaRow.items[i]
            if (!item) return
            if (mangaTab.popularShelf) mangaTab.seriesByIdRequested(item.title, item.malId)
            else mangaTab.seriesRequested(item.caption)
        }
        onExploreClicked: mangaTab.discoverPinRequested({ type: "manga", catalogId: "popular",
                                                          filterGroup: "", filterKey: "" })
    }

    Text {
        width: parent.width
        visible: mangaTab.shelvesLoading || mangaTab.shelfError.length > 0
        text: mangaTab.shelvesLoading ? "Loading manga shelves…" : "More manga shelves are unavailable. Retry"
        color: "#b8bcc8"
        font.pixelSize: 14
        wrapMode: Text.Wrap
        activeFocusOnTab: !mangaTab.shelvesLoading
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onReturnPressed: mangaTab.loadShelves(false, true)
        Keys.onSpacePressed: mangaTab.loadShelves(false, true)
        MouseArea { anchors.fill: parent; enabled: !mangaTab.shelvesLoading; cursorShape: Qt.PointingHandCursor; onClicked: mangaTab.loadShelves(false, true) }
    }

    Repeater {
        id: shelfRepeater
        model: mangaTab.additionalShelves
        onItemAdded: ++mangaTab._shelfRevision
        onItemRemoved: ++mangaTab._shelfRevision
        delegate: LazyPosterShelf {
            required property var modelData
            row: modelData
            geometryReady: !mangaTab._layingOut
            objectName: "tankobanMangaShelf_" + row.key
            width: mangaTab.width
            viewportTop: mangaTab.viewportTop
            viewportHeight: mangaTab.pageFlick ? mangaTab.pageFlick.height : 0
            onItemRequested: function(item) { mangaTab.seriesByIdRequested(item.title, item.malId) }
            onSeeAllRequested: function(pin) { mangaTab.discoverPinRequested(pin) }
        }
    }

    GenreMosaic {
        id: genreRow
        title: "Explore by Genre — Manga"
        genres: Catalog.genresManga
        // A genre tile opens the existing GenrePage (unchanged direct route). The genre
        // FILTER pin is resolvable by the adapter (the harness verifies it round-trips),
        // and a future per-genre Discover affordance can emit it; attaching it to the tile
        // click here would double-navigate (GenrePage + Discover switch).
        onGenreClicked: (i) => mangaTab.genreRequested(Catalog.genresManga[i].name)
        onExploreClicked: mangaTab.genreIndexRequested()
    }
}
