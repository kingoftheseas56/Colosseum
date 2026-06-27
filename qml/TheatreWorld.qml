// TheatreWorld - the real Colosseum world page for movies and series.
// Owner: A4. Shape is Harbor/TB3's home spine translated into the Colosseum board:
// Featured carousel, Continue Watching, ranked movies, ranked series, and a genre mosaic.

import QtQuick
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi

WorldPage {
    id: theatre
    medium: "Theatre"

    property var featuredRows: Catalog.theatreFeatured
    property var continueRows: Catalog.theatreContinue
    property var movieRows: Catalog.theatreTopMovies
    property var seriesRows: Catalog.theatreTopSeries
    property var animeRows: []

    Component.onCompleted: TheatreApi.loadTheatre(function(rows) {
        if (rows.featured.length > 0)
            theatre.featuredRows = rows.featured
        else {
            var f = []
            if (rows.movies.length > 0) f.push(rows.movies[0])
            if (rows.series.length > 0) f.push(rows.series[0])
            if (rows.anime.length > 0) f.push(rows.anime[0])
            if (f.length > 0) theatre.featuredRows = f
        }
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
    }

    ContinueRow {
        title: "Continue Watching"
        items: theatre.continueRows
    }

    TrendingTop10 {
        title: "Top Movies"
        items: theatre.movieRows
    }

    TrendingTop10 {
        title: "Top Series"
        items: theatre.seriesRows
    }

    TrendingTop10 {
        visible: theatre.animeRows.length > 0
        title: "Top Anime"
        items: theatre.animeRows
    }

    GenreMosaic {
        title: "Browse Theatre"
        genres: Catalog.theatreGenres
    }
}
