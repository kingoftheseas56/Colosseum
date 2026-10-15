// TankobanComicsTab — the Comics half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the comics rows. Data is passed IN from TankobanWorld (which owns the
// one-time ComicsCatalog.shelf compute + GcApi.explore fetch) so switching tabs never
// re-fetches — the Loader may rebuild this view, but the data is cached upstream and bound
// in reactively. Emits the comics signals the world forwards to the host. No manga knowledge.
import QtQuick

Column {
    id: comicsTab
    property Flickable pageFlick: null
    property Item backdrop: null
    property var parkRowHandler: null
    readonly property real contentLeft: rowIndex.contentLeft
    readonly property real _availableWidth: parent ? parent.width : 0
    readonly property var rowIndexRows: {
        comicsTab.comicShelves
        shelfRepeater.count
        var out = [
            { key: "collection", title: "Your Collection", target: collectionRow },
            { key: "top-comics", title: "Top in Tankoban — Comics", target: topComicsRow }
        ]
        for (var i = 0; i < shelfRepeater.count; i++) {
            var shelf = shelfRepeater.itemAt(i)
            var shelfRow = comicsTab.comicShelves[i]
            if (shelf && shelf.visible && shelfRow)
                out.push({ key: "shelf-" + i, title: shelfRow.label, target: shelf })
        }
        out.push({ key: "explore", title: "Explore Comics", target: genreRow })
        return out
    }
    x: comicsTab.contentLeft
    width: Math.max(0, comicsTab._availableWidth - comicsTab.contentLeft)
    spacing: 36

    RowIndexSidebar {
        id: rowIndex
        parent: comicsTab.parent
        z: 2
        x: 0
        pageFlick: comicsTab.pageFlick
        flowHost: comicsTab
        backdrop: comicsTab.backdrop
        worldName: "Tankoban"
        automationPrefix: "tankobanRowIndex"
        rows: comicsTab.rowIndexRows
        onRowRequested: (target) => { if (comicsTab.parkRowHandler) comicsTab.parkRowHandler(target) }
    }

    property var comicRows: []       // top-comics list (RCO-ranked, from the world)
    property var comicShelves: []    // [{label, rows}] browse shelves
    property var comicBoxes: []      // explore mosaic boxes (GetComics taxonomy)
    property var comicCovers: []     // explore mosaic art pool
    // The world's viewport in this tab's coordinates, bound by TankobanWorld. A height of 0
    // means unknown, and every shelf builds (harnesses, tests).
    property real viewportTop: 0
    property real viewportHeight: 0

    signal westernRequested(string title)
    signal westernExploreRequested(var box)
    signal comicSeriesRequested(var d)
    signal gcdSeriesRequested(var d)
    // Bubbles a tap on a Your Collection tile up to the world (comics-filtered).
    signal collectionOpenRequested(var entry)
    // Task 8: a See-all door on a Comics shelf emits a Discover pin. The world switches to
    // Discover and applies it. Pin shape (spec 3.6): {type,catalogId,filterGroup,filterKey}.
    signal discoverPinRequested(var pin)
    signal contentLeftUpdated(real value)
    onContentLeftChanged: comicsTab.contentLeftUpdated(comicsTab.contentLeft)

    ContinueRow {
        id: collectionRow
        title: "Your Collection"
        showSeeAll: false
        items: (Collection.revision, Collection.items("tankoban").filter(function(e) { return e.type === "comic" }))
        forgetHandler: function(e) { Collection.remove("tankoban", String(e.id)) }
        onDetailRequested: function(item) { collectionOpenRequested(item) }
        onResumeRequested: function(item) { collectionOpenRequested(item) }
    }

    // "Top in Tankoban — Comics" → Comics / Popular. The Explore wall retired in 2026-07-18
    // for the OLD fetch-all model; Task 8 re-arms it as a Discover pin into the Popular
    // comics catalogue. A tile tap still routes to the LOCG/GCD series door (unchanged).
    TrendingTop10 {
        id: topComicsRow
        title: "Top in Tankoban — Comics"
        items: comicsTab.comicRows.slice(0, 10)
        onItemClicked: (i) => {
            var topComics = comicsTab.comicRows.slice(0, 10)
            var it = topComics[i]
            if (!it) return
            if (it && it.locgId) comicsTab.comicSeriesRequested({ id: it.locgId, title: it.caption, cover: it.cover })
            else comicsTab.westernRequested(it.caption)
        }
        onExploreClicked: comicsTab.discoverPinRequested({ type: "comics", catalogId: "popular",
                                                           filterGroup: "", filterKey: "" })
    }

    // Catalogue shelf rows (browse-landing): Most Stocked, publisher, decade, deep, fan-made.
    // Pinnable shelves (Task 8): Most Stocked → {comics,most-stocked}; Marvel/DC/Image →
    //   {comics,popular,publisher:<lowercase arg>}. Non-pinnable shelves (decade/deep/fanmade)
    //   have no honest Discover filter, so they keep navigable:false and no See-all door.
    // ~15 shelves of up to 24 covers each. Built all at once they froze the first Comics
    // open (~0.8 s, profiled 2026-09-29), so each slot reserves the shelf's exact height and
    // builds its shelf once it comes within a screen of view, then keeps it.
    Repeater {
        id: shelfRepeater
        model: comicsTab.comicShelves
        delegate: Item {
            id: shelfSlot
            required property var modelData
            width: comicsTab.width
            visible: shelfSlot.modelData.rows.length > 0
            height: 30 + 14 + 212        // WidgetHeader + TrendingTop10 spacing + strip
            property bool built: false
            readonly property bool near: comicsTab.viewportHeight <= 0
                || (shelfSlot.y < comicsTab.viewportTop + 2 * comicsTab.viewportHeight
                    && shelfSlot.y + shelfSlot.height > comicsTab.viewportTop - comicsTab.viewportHeight)
            onNearChanged: if (shelfSlot.near) shelfSlot.built = true
            Component.onCompleted: if (shelfSlot.near) shelfSlot.built = true

            Loader {
                width: parent.width
                active: shelfSlot.built
                asynchronous: true
                sourceComponent: TrendingTop10 {
                    readonly property var modelData: shelfSlot.modelData
                    title: modelData.label
                    // Most Stocked and the three publishers are pinnable; everything else is not.
                    navigable: !!modelData.catalogId || modelData.kind === "stocked" || modelData.kind === "publisher"
                    items: modelData.rows
                    onItemClicked: (i) => {
                        var it = modelData.rows[i]
                        if (!it) return
                        if (it.locgId) comicsTab.comicSeriesRequested({ id: it.locgId, title: it.title, cover: it.cover })
                        else comicsTab.gcdSeriesRequested({ gcd: true, gcdId: it.gcdId, title: it.title, cover: it.cover })
                    }
                    onExploreClicked: {
                        if (modelData.catalogId)
                            comicsTab.discoverPinRequested({ type: "comics", catalogId: modelData.catalogId,
                                                            filterGroup: modelData.filterGroup || "",
                                                            filterKey: String(modelData.filterKey || "").toLowerCase() })
                        else if (modelData.kind === "stocked")
                            comicsTab.discoverPinRequested({ type: "comics", catalogId: "most-stocked",
                                                            filterGroup: "", filterKey: "" })
                        else if (modelData.kind === "publisher")
                            comicsTab.discoverPinRequested({ type: "comics", catalogId: "popular",
                                                            filterGroup: "publisher",
                                                            filterKey: String(modelData.arg || "").toLowerCase() })
                    }
                }
            }
        }
    }

    GenreMosaic {
        id: genreRow
        title: "Explore Comics"
        genres: comicsTab.comicBoxes
        covers: comicsTab.comicCovers
        navigable: false
        onGenreClicked: (i) => {
            var box = comicsTab.comicBoxes[i]
            if (box) comicsTab.discoverPinRequested({ type: "comics", catalogId: "popular",
                filterGroup: "genre", filterKey: String(box.name || "").toLowerCase() })
        }
    }
}
