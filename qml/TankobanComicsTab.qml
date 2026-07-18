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

    TrendingTop10 {
        title: "Top in Tankoban — Comics"
        navigable: false          // the Explore wall retired 2026-07-18 — no See-all target
        items: comicsTab.comicRows.slice(0, 10)
        onItemClicked: (i) => {
            var topComics = comicsTab.comicRows.slice(0, 10)
            var it = topComics[i]
            if (!it) return
            if (it && it.locgId) comicsTab.comicSeriesRequested({ id: it.locgId, title: it.caption, cover: it.cover })
            else comicsTab.westernRequested(it.caption)
        }
    }

    // Catalogue shelf rows (browse-landing): Most Stocked, publisher, decade, deep, fan-made.
    Repeater {
        model: comicsTab.comicShelves
        delegate: TrendingTop10 {
            required property var modelData
            title: modelData.label
            navigable: false          // shelf rows have no explore target either
            items: modelData.rows
            visible: modelData.rows.length > 0
            onItemClicked: (i) => {
                var it = modelData.rows[i]
                if (it) comicsTab.gcdSeriesRequested({ gcd: true, gcdId: it.gcdId, title: it.title, cover: it.cover })
            }
        }
    }

    GenreMosaic {
        title: "Explore Comics"
        genres: comicsTab.comicBoxes
        covers: comicsTab.comicCovers
        navigable: false
        onGenreClicked: (i) => comicsTab.westernExploreRequested(comicsTab.comicBoxes[i])
    }
}
