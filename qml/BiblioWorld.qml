// BiblioWorld - the Colosseum world page for books. Owner: A2.
// Same spine as Tankoban/Theatre: Featured carousel, Continue, Top-10, genres.
//
// Discovery = Apple Books charts via BiblioApi (live, daily-fresh). Catalog.biblio* is the static
// fallback so the page paints instantly and never sits empty if the live call is slow. Delivery
// (search + download) stays libgen from TB2 - a separate layer, like Cinemeta vs the Theatre addon.

import QtQuick
import "Catalog.js" as Catalog
import "BiblioApi.js" as BiblioApi

WorldPage {
    id: biblio
    medium: "Biblio"

    property var featuredRows: Catalog.biblioFeatured
    property var topRows: Catalog.biblioTop
    property var genreRows: Catalog.biblioGenres
    property var topSeriesRows: []          // canonical "Books vs Series" discovery shelf (local graph)
    property bool canonicalTopLoaded: false
    signal biblioGenreRequested(string genreName)
    signal biblioSeriesRequested(string series, string author)

    function loadDiscoveryRows() {
        if (typeof SeriesIndex !== "undefined" && SeriesIndex.topBooks) {
            var canonicalTop = SeriesIndex.topBooks(10)
            if (canonicalTop && canonicalTop.length > 0) {
                biblio.topRows = canonicalTop
                biblio.canonicalTopLoaded = true
            }
        }

        // Top Series is a purely canonical browse surface (no Apple fallback): the crown Books-vs-Series
        // feature, surfaced for discovery. Gradient stack tiles need no cover art, so the local graph
        // carries it end to end.
        if (typeof SeriesIndex !== "undefined" && SeriesIndex.topSeries) {
            var canonicalSeries = SeriesIndex.topSeries(12)
            if (canonicalSeries && canonicalSeries.length > 0)
                biblio.topSeriesRows = canonicalSeries
        }

        // Keep Apple's live hero art, but do not clobber a healthy canonical top row.
        BiblioApi.loadBiblio(function(rows) {
            if (rows.featured && rows.featured.length > 0)
                biblio.featuredRows = rows.featured
            if (!biblio.canonicalTopLoaded && rows.top && rows.top.length > 0)
                biblio.topRows = rows.top
        })
    }
    Component.onCompleted: loadDiscoveryRows()

    // tap a book → fetch its full detail by title, then open the dust-jacket page
    function openByTitle(title, author) {
        if (!title) return
        if (typeof SeriesIndex !== "undefined" && SeriesIndex.bookDetail) {
            var canonicalBook = SeriesIndex.bookDetail(title, author || "")
            if (canonicalBook && canonicalBook.title) {
                biblio.bookRequested(canonicalBook)
                return
            }
        }
        BiblioApi.lookupBook(title, author || "", function(b) { if (b) biblio.bookRequested(b) })
    }

    FeaturedCarousel {
        kicker: "Featured in Biblio"
        primaryLabel: "Read"
        secondaryLabel: "Details"
        slides: biblio.featuredRows
        onPrimaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "", biblio.featuredRows[i] ? biblio.featuredRows[i].author : "")
        onSecondaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "", biblio.featuredRows[i] ? biblio.featuredRows[i].author : "")
    }

    ContinueRow {
        title: "Continue"
        items: (Progress.revision, Progress.recent("book", 12))
        onResumeRequested: (item) => biblio.continueResumeRequested(item)
        onDetailRequested: (item) => biblio.continueDetailRequested(item)
    }

    TrendingTop10 {
        title: "Top 10 in Biblio"
        items: biblio.topRows
        onItemClicked: (i) => biblio.openByTitle(biblio.topRows[i] ? biblio.topRows[i].caption : "", biblio.topRows[i] ? biblio.topRows[i].author : "")
    }

    // ── Top Series (canonical) — the Books-vs-Series signature, in the house "Top …" language
    //    (ghost rank numerals + strip), tiles are gradient stacks with a gold "N books" chip. ──
    TrendingTopSeries {
        title: "Top Series in Biblio"
        items: biblio.topSeriesRows
        visible: biblio.topSeriesRows.length > 0
        onSeriesClicked: (i) => biblio.biblioSeriesRequested(
            biblio.topSeriesRows[i] ? biblio.topSeriesRows[i].series : "",
            biblio.topSeriesRows[i] ? biblio.topSeriesRows[i].author : "")
    }

    GenreMosaic {
        title: "Browse Biblio"
        genres: biblio.genreRows
        onGenreClicked: (i) => biblio.biblioGenreRequested(biblio.genreRows[i].name)
    }
}
