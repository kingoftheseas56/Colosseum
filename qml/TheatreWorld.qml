// TheatreWorld - Colosseum's Theatre catalog shell.
// Owner: A4. Persistent top: Featured, Continue Watching, then Harbor-shaped pages
// under four tabs: Discover, Movies, Shows, Anime.

import QtQuick
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi

WorldPage {
    id: theatre
    medium: "Theatre"

    // Theatre carries the full tile object (Cinemeta id + type) up to Main for detail routing.
    signal theatreItemRequested(var item)

    property var featuredRows: Catalog.theatreFeatured
    // Real "Continue Watching" from the Progress store (what you actually started).
    property int progressRevision: Progress.revision
    property var continueRows: Progress.recent("video", 12)
    property var movieRows: Catalog.theatreTopMovies
    property var seriesRows: Catalog.theatreTopSeries
    property var animeRows: []
    property string activeTab: "discover"

    onProgressRevisionChanged: continueRows = Progress.recent("video", 12)

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
        if (rows.movies.length > 0)
            theatre.movieRows = rows.movies
        if (rows.series.length > 0)
            theatre.seriesRows = rows.series
        if (rows.anime.length > 0)
            theatre.animeRows = rows.anime
        if (rows.featured.length > 0)
            theatre.featuredRows = rows.featured
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
        onResumeRequested: (item) => theatre.continueResumeRequested(item)
        onDetailRequested: (item) => theatre.continueDetailRequested(item)
    }

    TheatreTabBar {
        backdrop: theatre.backdrop
        currentTab: theatre.activeTab
        onTabRequested: (tab) => theatre.activeTab = tab
    }

    TheatreCatalogPage {
        pageKey: theatre.activeTab
        onItemRequested: (item) => theatre.theatreItemRequested(
            theatre.itemWithIdentity(item, item.type === "movie" ? "movie" : "series"))
    }
}
