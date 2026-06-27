// BiblioWorld - the Colosseum world page for books. Owner: A2.
// Same spine as Tankoban/Theatre, trimmed to the agreed BASE: Featured carousel, Top-10, genres.
// (No Continue row yet - there's no reading-progress to feed it; that comes "on top" later.)
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
    signal biblioGenreRequested(string genreName)

    // Live override: swap in Apple's fresh chart once it lands; keep the static fallback on failure.
    Component.onCompleted: BiblioApi.loadBiblio(function(rows) {
        if (rows.featured && rows.featured.length > 0)
            biblio.featuredRows = rows.featured
        if (rows.top && rows.top.length > 0)
            biblio.topRows = rows.top
    })

    // tap a book → fetch its full detail by title, then open the dust-jacket page
    function openByTitle(title) {
        if (!title) return
        BiblioApi.lookupBook(title, function(b) { if (b) biblio.bookRequested(b) })
    }

    FeaturedCarousel {
        kicker: "Featured in Biblio"
        primaryLabel: "Read"
        secondaryLabel: "Details"
        slides: biblio.featuredRows
        onPrimaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "")
        onSecondaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "")
    }

    TrendingTop10 {
        title: "Top 10 in Biblio"
        items: biblio.topRows
        onItemClicked: (i) => biblio.openByTitle(biblio.topRows[i] ? biblio.topRows[i].caption : "")
    }

    GenreMosaic {
        title: "Browse Biblio"
        genres: biblio.genreRows
        onGenreClicked: (i) => biblio.biblioGenreRequested(biblio.genreRows[i].name)
    }
}
