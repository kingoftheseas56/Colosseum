// TankobanComicsTab — the Comics half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the comics rows. Data is passed IN from TankobanWorld (which owns the
// one-time ComicsCatalog.shelf compute + GcApi.explore fetch) so switching tabs never
// re-fetches — the Loader may rebuild this view, but the data is cached upstream and bound
// in reactively. Emits the comics signals the world forwards to the host. No manga knowledge.
import QtQuick

Column {
    id: comicsTab
    width: parent ? parent.width : 0
    spacing: 36

    property var comicRows: []       // top-comics list (RCO-ranked, from the world)
    property var comicShelves: []    // [{label, rows}] browse shelves
    property var comicBoxes: []      // explore mosaic boxes (GetComics taxonomy)
    property var comicCovers: []     // explore mosaic art pool

    signal westernRequested(string title)
    signal westernExploreRequested(var box)
    signal comicSeriesRequested(var d)
    signal gcdSeriesRequested(var d)
    // Bubbles a tap on a Your Collection tile up to the world (comics-filtered).
    signal collectionOpenRequested(var entry)
    // Task 8: a See-all door on a Comics shelf emits a Discover pin. The world switches to
    // Discover and applies it. Pin shape (spec 3.6): {type,catalogId,filterGroup,filterKey}.
    signal discoverPinRequested(var pin)

    ContinueRow {
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
    Repeater {
        model: comicsTab.comicShelves
        delegate: TrendingTop10 {
            required property var modelData
            title: modelData.label
            // Most Stocked and the three publishers are pinnable; everything else is not.
            navigable: !!modelData.catalogId || modelData.kind === "stocked" || modelData.kind === "publisher"
            items: modelData.rows
            visible: modelData.rows.length > 0
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

    GenreMosaic {
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
