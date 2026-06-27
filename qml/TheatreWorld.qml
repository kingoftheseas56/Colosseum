// TheatreWorld - the real Colosseum world page for movies and series.
// Owner: A4. Shape is Harbor/TB3's home spine translated into the Colosseum board:
// Featured carousel, Continue Watching, ranked movies, ranked series, and a genre mosaic.

import QtQuick
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi

WorldPage {
    id: theatre
    medium: "Theatre"

    // Theatre carries the full tile object (Cinemeta id + type) up to Main for detail routing.
    signal theatreItemRequested(var item)

    property var featuredRows: Catalog.theatreFeatured
    property var continueRows: Catalog.theatreContinue
    property var movieRows: Catalog.theatreTopMovies
    property var seriesRows: Catalog.theatreTopSeries
    property var animeRows: []

    function idFromArt(item) {
        var fields = [item.cover || "", item.art || ""]
        for (var i = 0; i < fields.length; i++) {
            var m = String(fields[i]).match(/\/(tt\d+)\/img/)
            if (m && m[1])
                return m[1]
        }
        return ""
    }

    function itemWithIdentity(item, fallbackType) {
        var out = {}
        for (var k in item)
            out[k] = item[k]
        if (!out.id)
            out.id = idFromArt(item)
        if (!out.type)
            out.type = fallbackType
        return out
    }

    Component.onCompleted: TheatreApi.loadTheatre(function(rows) {
        if (rows.movies.length > 0) {
            theatre.movieRows = rows.movies
            theatre.continueRows = rows.movies.slice(0, 2).concat(rows.series.slice(0, 2), rows.anime.slice(0, 1))
        }
        if (rows.series.length > 0)
            theatre.seriesRows = rows.series
        if (rows.anime.length > 0)
            theatre.animeRows = rows.anime
    })

    FeaturedCarousel {
        kicker: "Featured in Theatre"
        primaryLabel: "Watch"
        secondaryLabel: "Details"
        slides: theatre.featuredRows
        // Watch + Details both open the detail page for now (Watch goes straight to play once the
        // player hook lands). Type comes from the slide's ghost marker ("S" = series, else movie).
        onPrimaryClicked: (index) => theatre.theatreItemRequested(
            theatre.itemWithIdentity(theatre.featuredRows[index], theatre.featuredRows[index].ghost === "S" ? "series" : "movie"))
        onSecondaryClicked: (index) => theatre.theatreItemRequested(
            theatre.itemWithIdentity(theatre.featuredRows[index], theatre.featuredRows[index].ghost === "S" ? "series" : "movie"))
    }

    ContinueRow {
        title: "Continue Watching"
        items: theatre.continueRows
        onItemClicked: (index) => theatre.theatreItemRequested(
            theatre.itemWithIdentity(theatre.continueRows[index], "movie"))
    }

    TrendingTop10 {
        title: "Top Movies"
        items: theatre.movieRows
        onItemClicked: (index) => theatre.theatreItemRequested(theatre.itemWithIdentity(theatre.movieRows[index], "movie"))
    }

    TrendingTop10 {
        title: "Top Series"
        items: theatre.seriesRows
        onItemClicked: (index) => theatre.theatreItemRequested(theatre.itemWithIdentity(theatre.seriesRows[index], "series"))
    }

    TrendingTop10 {
        visible: theatre.animeRows.length > 0
        title: "Top Anime"
        items: theatre.animeRows
        onItemClicked: (index) => theatre.theatreItemRequested(theatre.itemWithIdentity(theatre.animeRows[index], "series"))
    }

    GenreMosaic {
        title: "Browse Theatre"
        genres: Catalog.theatreGenres
    }
}
