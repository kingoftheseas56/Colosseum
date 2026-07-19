// TankobanMangaTab — the Manga half of the Tankoban world's browse (spec 2026-07-18).
// A plain Column of the manga rows, sourcing static manga data from Catalog.js and
// emitting the same signals the world forwards to the host. No comics knowledge.
import QtQuick
import "Catalog.js" as Catalog

Column {
    id: mangaTab
    width: parent ? parent.width : 0
    spacing: 36

    signal seriesRequested(string title)
    signal genreRequested(string name)
    signal genreIndexRequested()
    // Bubbles a tap on a Your Collection tile up to the world (manga-filtered).
    signal collectionOpenRequested(var entry)

    ContinueRow {
        title: "Your Collection"
        showSeeAll: false
        items: (Collection.revision, Collection.items("tankoban").filter(function(e) { return e.type === "manga" }))
        forgetHandler: function(e) { Collection.remove("tankoban", String(e.id)) }
        onDetailRequested: function(item) { collectionOpenRequested(item) }
        onResumeRequested: function(item) { collectionOpenRequested(item) }
    }

    TrendingTop10 {
        title: "Top in Tankoban — Manga"
        items: Catalog.topManga
        onItemClicked: (i) => mangaTab.seriesRequested(Catalog.topManga[i].caption)
    }

    GenreMosaic {
        title: "Explore by Genre — Manga"
        genres: Catalog.genresManga
        onGenreClicked: (i) => mangaTab.genreRequested(Catalog.genresManga[i].name)
        onExploreClicked: mangaTab.genreIndexRequested()
    }
}
