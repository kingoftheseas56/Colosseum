// TankobanMangaTab — the Manga half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the manga rows, sourcing static manga data from Catalog.js and
// emitting the same signals the world forwards to the host. No comics knowledge.
import QtQuick
import "Catalog.js" as Catalog

Column {
    id: mangaTab
    property Flickable pageFlick: null
    property Item backdrop: null
    property var parkRowHandler: null
    readonly property real contentLeft: rowIndex.contentLeft
    readonly property real _availableWidth: parent ? parent.width : 0
    readonly property var rowIndexRows: [
        { key: "collection", title: "Your Collection", target: collectionRow },
        { key: "top-manga", title: "Top in Tankoban — Manga", target: topMangaRow },
        { key: "genres", title: "Explore by Genre — Manga", target: genreRow }
    ]
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
        title: "Top in Tankoban — Manga"
        items: Catalog.topManga
        // World-namespaced automation reach (catalogue-independence Slice 3, 2026-08-20):
        // a Lanista scenario needs a scroll-free click path into a real series page — this
        // rail is reachable without touching the Discover wall's un-named, un-scrollable
        // GridView (the Slice-2 documented bridge gap).
        namePrefix: "tankobanTopMangaTile_"
        onItemClicked: (i) => mangaTab.seriesRequested(Catalog.topManga[i].caption)
        onExploreClicked: mangaTab.discoverPinRequested({ type: "manga", catalogId: "popular",
                                                          filterGroup: "", filterKey: "" })
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
