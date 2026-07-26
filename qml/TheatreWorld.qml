// TheatreWorld - Colosseum's Theatre catalog shell.
// Owner: A4. Persistent top: Featured, Continue Watching, then Harbor-shaped pages
// under three tabs: Movies, Shows, Anime.

import QtQuick
import "Catalog.js" as Catalog
import "TheatreApi.js" as TheatreApi
import "Torrentio.js" as Torrentio
import "AddonClient.js" as AddonClient
import "EpisodeBrowser.js" as EpisodeBrowser
import "NextUp.js" as NextUp
import "LibraryApi.js" as LibraryApi

WorldPage {
    id: theatre
    medium: "Theatre"

    // Theatre carries the full tile object (Cinemeta id + type) up to Main for detail routing.
    signal theatreItemRequested(var item)
    signal theatreGenreRequested(string kind, string name)
    signal theatreGenreIndexRequested(string kind)
    // Bubbles a tap on a Your Collection tile up to Main's openCollectionEntry door.
    signal collectionOpenRequested(var entry)
    // Library tab (Stage 2) — the ⋮ menu's Resume / Mark-watched bubble up to Main's handlers.
    signal libraryResumeRequested(var entry)
    signal libraryMarkWatchedRequested(var entry, bool watched)
    // Next Up direct play — the exact TheatreSeries.playRequested shape, wired in Main
    // to the same openMovieSession door (spec 2026-07-18, Jellyfin library inheritance).
    signal playRequested(string infoHash, int fileIdx, string title, string backdropUrl, string subType, string subId, var streamCandidates, var playbackContext)

    property var featuredRows: Catalog.theatreFeatured
    // Real "Continue Watching" from the Progress store (what you actually started).
    property int progressRevision: Progress.revision
    property var continueRows: Progress.recent("video", 12)
    property var movieRows: Catalog.theatreTopMovies
    property var seriesRows: Catalog.theatreTopSeries
    property var animeRows: []
    property string activeTab: "discover"

    onProgressRevisionChanged: {
        continueRows = Progress.recent("video", 12)
        recomputeNextUp()
    }

    // ---- Next Up (spec 2026-07-18): "you finished the last episode — here's the next".
    // A show cards ONLY when its latest episode entry is finished (the 0.90 line);
    // otherwise it lives in Continue. Meta is fetched ONCE per show per app session
    // (the same TheatreApi call the series page makes) and cached here.
    property var nextUpRows: []
    property var _nextUpMeta: ({})      // show root -> meta (videos carrier)
    property var _nextUpPending: ({})   // show root -> true while a fetch is in flight

    function recomputeNextUp() {
        var fin = NextUp.finishedShows(Progress.recent("video", 48)).slice(0, 8)
        var out = []
        for (var i = 0; i < fin.length; i++) {
            var rec = fin[i]
            var meta = theatre._nextUpMeta[rec.show]
            if (meta === undefined) {
                theatre.fetchNextUpMeta(rec.show)
                continue
            }
            if (!meta || !meta.videos || !meta.videos.length)
                continue                        // meta came back empty — no honest card
            var next = NextUp.nextEpisodeFromMeta(meta.videos, rec.entry.id)
            if (next)
                out.push(NextUp.theatreCard(rec, next))
        }
        theatre.nextUpRows = out
    }

    function fetchNextUpMeta(show) {
        if (theatre._nextUpPending[show]) return
        theatre._nextUpPending[show] = true
        TheatreApi.loadMeta("series", show, function(meta) {
            theatre._nextUpMeta[show] = meta || null   // null caches the miss — never refetch-loop
            theatre.recomputeNextUp()
        })
    }

    // The circle on a Next Up card: resolve the fresh episode's sources and walk in the
    // series page's own door (playRequested → openMovieSession) with a full episode
    // queue, so prev/next and the drawer work from the first frame. No sources → the
    // series page (honest fallback, never a dead spinner).
    function playNextUp(item) {
        var showId = item.resume.showId
        var detail = { "id": showId, "type": "series", "title": item.title, "cover": item.cover }
        var onRows = function(rows) {
            if (!rows || !rows.length) { theatre.theatreItemRequested(detail); return }
            var meta = theatre._nextUpMeta[showId]
            var ctx = (meta && meta.videos)
                      ? EpisodeBrowser.queueContextFromMeta(meta.videos, item.id, item.title,
                                                            item.cover, meta.year || "")
                      : null
            var epTitle = item.title + " - S" + item.resume.season + "E" + item.resume.episode
            theatre.playRequested(rows[0].infoHash || "", rows[0].fileIdx || 0, epTitle,
                                  item.cover || "", "series", item.id, rows, ctx || ({}))
        }
        // Same source ladder as the player and the picker. This path used to call
        // Torrentio.js directly with no idea the store existed, so removing Torrentio
        // did nothing here and any well ranked above it was ignored. No source at all
        // falls through to onRows([]) — the series page, its own honest fallback.
        // (2026-07-25, A5 — Torrentio-honesty fix.)
        var installed = (typeof Extensions !== "undefined") ? Extensions.installed() : []
        var exts = AddonClient.streamExtensions(installed, "series", item.id)
        var lastResort = function() {
            if (AddonClient.torrentioEnabled(installed))
                Torrentio.loadStreams("series", item.id, onRows)
            else
                onRows([])
        }
        if (exts.length) {
            AddonClient.loadStreams(exts, "series", item.id, function() {}, function(rows) {
                if (rows && rows.length) onRows(rows)
                else lastResort()
            })
        } else {
            lastResort()
        }
    }

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

    Component.onCompleted: {
        recomputeNextUp()
        loadCatalog()
        // Refresh the Library's new-episode + airing stamps for saved series (spec §4.5).
        // Cheap: skips fresh-stamped (<6h) series and does zero network on an empty shelf.
        LibraryApi.refreshStamps(Collection.items("theatre"), TheatreApi.loadMeta,
            Progress.recent("video", 0),
            function(id) { return Progress.watchedMark(id) },
            function(id) { Progress.forget("video", id) },
            function(id, on) { Progress.setWatchedMark(id, on) },
            function(e) { Collection.add("theatre", e) },
            Date.now(), function() {})
    }
    function loadCatalog() { TheatreApi.loadTheatre(function(rows) {
        if (rows.movies.length > 0)
            theatre.movieRows = rows.movies
        if (rows.series.length > 0)
            theatre.seriesRows = rows.series
        if (rows.anime.length > 0)
            theatre.animeRows = rows.anime
        if (rows.featured.length > 0)
            theatre.featuredRows = rows.featured
    }) }

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

    // Next Up sits ABOVE Continue (Jellyfin's order — freshest intent first). Cards are
    // fresh episodes (no progress bar); the circle plays, elsewhere opens the series.
    ContinueRow {
        title: "Next Up"
        items: theatre.nextUpRows
        onResumeRequested: (item) => theatre.playNextUp(item)
        onDetailRequested: (item) => theatre.theatreItemRequested(
            { "id": item.resume.showId, "type": "series", "title": item.title, "cover": item.cover })
        onSeeAllRequested: theatre.continueSeeAllRequested()
    }

    ContinueRow {
        title: "Continue Watching"
        items: theatre.continueRows
        onResumeRequested: (item) => theatre.continueResumeRequested(item)
        onDetailRequested: (item) => theatre.continueDetailRequested(item)
        onSeeAllRequested: theatre.continueSeeAllRequested()
    }

    TheatreTabBar {
        backdrop: theatre.backdrop
        currentTab: theatre.activeTab
        onTabRequested: (tab) => theatre.activeTab = tab
    }

    DiscoverPage {
        id: discoverPage
        visible: theatre.activeTab === "discover"
        width: parent.width
        height: visible ? Math.max(620, theatre.height - 200) : 0
        onItemOpenRequested: (item) => theatre.theatreItemRequested(
            theatre.itemWithIdentity(item, item.type === "movie" ? "movie" : "series"))
    }

    TheatreCatalogPage {
        visible: theatre.activeTab === "movies" || theatre.activeTab === "shows" || theatre.activeTab === "anime"
        height: visible ? implicitHeight : 0
        pageKey: (theatre.activeTab === "movies" || theatre.activeTab === "shows" || theatre.activeTab === "anime")
                 ? theatre.activeTab : "movies"
        onItemRequested: (item) => theatre.theatreItemRequested(
            theatre.itemWithIdentity(item, item.type === "movie" ? "movie" : "series"))
        onGenreRequested: (kind, name) => theatre.theatreGenreRequested(kind, name)
        onGenreIndexRequested: (kind) => theatre.theatreGenreIndexRequested(kind)
        onDiscoverPinRequested: (pin) => {
            theatre.activeTab = "discover"
            discoverPage.applyPin(pin)
        }
    }

    // Library — the fifth tab (Stage 2). The saved shelf, life-marked, with the ⋮ menu.
    LibraryPage {
        visible: theatre.activeTab === "library"
        width: parent.width
        height: visible ? Math.max(620, theatre.height - 200) : 0
        onResumeRequested: (e) => theatre.libraryResumeRequested(e)
        onDetailRequested: (e) => theatre.collectionOpenRequested(e)
        onDismissRequested: (e) => { if (typeof Progress !== "undefined") Progress.forget("video", String(e.id)) }
        onMarkWatchedRequested: (e, w) => theatre.libraryMarkWatchedRequested(e, w)
        onRemoveRequested: (e) => { if (typeof Collection !== "undefined") Collection.remove("theatre", String(e.id)) }
    }
}
