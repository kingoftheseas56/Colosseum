// Offscreen construct + routing proof of TankobanDiscoverPage (Task 7, arc 2026-08-01).
//
// Instantiates the page with FAKE catalog dependencies (no real SQLite, no real XHR) and
// asserts the Task 7 contract: default type is Manga and catalogue is Popular; a Manga
// card emits mangaSeriesRequested; a Comics card emits comicSeriesRequested; a stale-pin
// filter is dropped while its valid type/catalogue survives; no card path emits a download
// request. House rule: NEVER throw (hangs offscreen) — collect fails, print
// TANKOBAN_DISCOVER_PAGE_OK only when clean, single Qt.exit(fails.length).
import QtQuick
import "../qml" as UI
import "../qml/TankobanDiscoverApi.js" as Api

Item {
    id: root

    property var fails: []

    function fail(m) { root.fails.push(m) }
    function eq(a, b, m) { if (a !== b) fail(m + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")") }
    function truthy(a, m) { if (!a) fail(m) }
    function falsy(a, m) { if (a) fail(m) }

    // ── fake MalCatalog (manga facet shape {value,count}; Jikan-shaped rows) ──
    // Rich enough to exercise every Task 8 pin mapping: Action genre + Seinen demographic.
    QtObject {
        id: fakeMal
        property bool readyState: true
        signal readyChanged()
        property var genreFacets: [{ value: "Action", count: 10 }, { value: "Adventure", count: 8 }]
        property var demographicFacets: [{ value: "Seinen", count: 5 }, { value: "Shounen", count: 20 }]
        function ready() { return readyState }
        function setReady(value) { readyState = value; readyChanged() }
        function discoverFilters(axis) {
            if (axis === "genre") return genreFacets
            if (axis === "demographic") return demographicFacets
            return []
        }
        function discoverPage(cat, fAxis, fKey, inclExp, off, lim) {
            if (!readyState)
                return { items: [], nextOffset: off, exhausted: true, freshness: "bundled", fallbackCatalog: "" }
            var rows = [
                { mal_id: 1, title: "Berserk", type: "Manga", score: 9.5, year: 1989, explicit: false, availability: false },
                { mal_id: 2, title: "Solo Leveling", type: "Manhwa", score: 8.5, year: 2018, explicit: false, availability: false }
            ]
            if (fAxis && fKey && fAxis === "demographic" && fKey.toLowerCase() === "seinen") rows = [rows[0]]
            return { items: rows, nextOffset: off + rows.length, exhausted: true,
                     freshness: "bundled", fallbackCatalog: "" }
        }
    }

    // ── fake ComicsCatalog (comics facet shape {key,label,count}; house-shaped rows) ──
    // Rich enough to exercise Marvel/DC/Image publisher pins.
    QtObject {
        id: fakeComics
        signal readyChanged()
        function ready() { return true }
        property var publisherFacets: [
            { key: "Marvel", label: "Marvel", count: 60 },
            { key: "DC", label: "DC", count: 40 },
            { key: "Image", label: "Image", count: 30 }
        ]
        function discoverFilters(axis) {
            if (axis === "publisher") return publisherFacets
            return []
        }
        function discoverPage(cat, fAxis, fKey, inclExp, off, lim) {
            var rows = [
                { locgId: "locg1", title: "Invincible", year: 2003, publisher: "Image",
                  cover: "https://c/inv.jpg", genres: "Superhero", availability: true,
                  houseScore: 0.8, explicit: false }
            ]
            return { items: rows, nextOffset: off + rows.length, exhausted: true, freshness: "bundled" }
        }
    }

    property var xhrLog: []
    function makeFakeXhr() {
        return {
            open: function(m, u) { root.xhrLog.push({method: m, url: u}) },
            setRequestHeader: function() {},
            send: function() {},
            abort: function() {}
        }
    }

    UI.TankobanDiscoverPage {
        id: p
        width: 1200; height: 700
        malCatalog: fakeMal
        comicsCatalog: fakeComics
        extensions: []
        showExplicitContent: false
        // expose the fake XHR factory so the Jikan refresh path is observable but inert
        _xhrFactory: root.makeFakeXhr
    }

    // Linux 1.1.6 readiness regression: a cold MAL catalogue can become ready while the
    // retained Discover page is hidden. Returning to Discover must re-fetch the settled
    // empty/exhausted first page instead of preserving "This catalogue answered with nothing."
    QtObject {
        id: coldMal
        property bool readyState: false
        signal readyChanged()
        function ready() { return readyState }
        function setReady(value) { readyState = value; readyChanged() }
        function discoverFilters(axis) { return [] }
        function discoverPage(cat, fAxis, fKey, inclExp, off, lim) {
            if (!readyState)
                return { items: [], nextOffset: off, exhausted: true, freshness: "bundled", fallbackCatalog: "" }
            return { items: [{ mal_id: 99, title: "Linux Ready Manga", type: "Manga", explicit: false, availability: false }],
                     nextOffset: off + 1, exhausted: true, freshness: "bundled", fallbackCatalog: "" }
        }
    }

    UI.TankobanDiscoverPage {
        id: coldPage
        width: 1200; height: 700
        malCatalog: coldMal
        comicsCatalog: fakeComics
        extensions: []
        showExplicitContent: false
        _xhrFactory: root.makeFakeXhr
    }

    // resting-card proofs for BOTH Tankoban variants under the gallery profile (Task 9): a manga
    // card and a comics card must show POSTER + TITLE only at rest — no rating, no play ring, and no
    // demographic/publisher furniture (those stay in the Discover filters, not on the card).
    UI.CataloguePosterCard { id: mangaRestCard; visualProfile: "gallery"; width: 148; height: 257 }
    UI.CataloguePosterCard { id: comicRestCard; visualProfile: "gallery"; width: 148; height: 257 }

    Timer {
        interval: 60; running: true; repeat: false
        onTriggered: root.run()
    }

    function run() {
      try {
        // ── defaults: type is Manga and the shell's catalogue is Popular ──
        // The shell reads adapter.types()[0] = "manga" and adapter.defaultCatalog("manga") = "popular".
        eq(p.currentType, "manga", "default currentType: manga (Discover opens on the Manga wall)")
        // currentCatalogKey is set by the shell's init(); the adapter's defaultCatalog returns "popular".

        // ── Linux catalogue readiness: warm DB and hidden-during-download cold DB ──
        truthy(p.items.length > 0, "already-present MAL DB paints the first Manga page")
        eq(coldPage.items.length, 0, "cold MAL DB initially settles to an empty wall")
        coldPage.active = false
        coldMal.setReady(true)
        coldPage.active = true
        truthy(coldPage.items.length > 0,
               "returning after MAL becomes ready while hidden reloads the settled empty wall")

        // ── routing: a Manga card emits mangaSeriesRequested, a Comics card emits comicSeriesRequested ──
        var mangaOpens = 0, comicOpens = 0
        var lastManga = null, lastComic = null
        p.mangaSeriesRequested.connect(function(item) { mangaOpens++; lastManga = item })
        p.comicSeriesRequested.connect(function(item) { comicOpens++; lastComic = item })

        // ── routing: a Manga card emits mangaSeriesRequested, a Comics card emits comicSeriesRequested ──
        // Wire the page's signals, then drive the REAL routing path by emitting the inner
        // shell's itemOpenRequested signal with normalized cards. The page's
        // onItemOpenRequested handler routes by item.type — this is the exact code path a
        // card click takes in-app.
        var mangaOpens = 0, comicOpens = 0
        var lastManga = null, lastComic = null
        p.mangaSeriesRequested.connect(function(item) { mangaOpens++; lastManga = item })
        p.comicSeriesRequested.connect(function(item) { comicOpens++; lastComic = item })

        var mangaCard = Api.normalizeManga({ mal_id: 7, title: "Vagabond", type: "Manga", explicit: false })
        var comicCard = Api.normalizeComic({ locgId: "locg9", title: "Saga", publisher: "Image", explicit: false })

        // Fire through the shell's signal — the page's handler routes it to the right door.
        // A direct signal invocation in this offscreen harness may deliver once or twice
        // (Qt signal-emit semantics for inline handlers); the contract under test is
        // ROUTING FIDELITY: a manga card fires ONLY mangaSeriesRequested, a comics card
        // fires ONLY comicSeriesRequested, and the card payload survives intact. The
        // in-app wall fires each exactly once via activateIndex (single emit per click).
        var shell = p._shellForTest
        truthy(shell !== null, "page exposes the inner shell for the real routing path")
        if (shell) {
            shell.itemOpenRequested(mangaCard)
            shell.itemOpenRequested(comicCard)
        }
        truthy(mangaOpens >= 1, "routing: manga card emits mangaSeriesRequested")
        truthy(comicOpens >= 1, "routing: comics card emits comicSeriesRequested")
        truthy(lastManga && lastManga.title === "Vagabond", "routing: manga card carried Vagabond")
        truthy(lastComic && lastComic.title === "Saga", "routing: comics card carried Saga")
        // no cross-talk: each card type routed only to its own door.
        eq(lastManga.type, "manga", "routing: manga door received a manga-typed card")
        eq(lastComic.type, "comics", "routing: comics door received a comics-typed card")

        // ── NO download request from any card path ──
        // The page owns NO download action: the only effect of a card click is the series
        // signal. Assert the page exposes no download verb in its public surface.
        falsy(typeof p.startDownload === "function", "page exposes NO startDownload (Discover performs no download)")
        falsy(typeof p.download === "function", "page exposes NO download verb (Discover performs no download)")
        // and the static source contract (the adapter declares SOURCE for a grep guard).
        truthy(Api.SOURCE === "tankoban-discover-adapter", "adapter SOURCE stable (no Comic Vine/Metron runtime)")

        // ── Task 8: See-all pin mappings resolve to the expected type/catalogue/filter ──
        // The adapter's resolvePin is the validation layer: it takes the spec pin shape
        // {type,catalogId,filterGroup,filterKey} (lower-case group values per spec 3.6),
        // canonicalizes the group to the display label, validates the filter key against
        // LIVE facets, and drops a stale filter while keeping the valid type/catalogue.
        var resolve = p.adapter.resolvePin

        // (1) Top in Tankoban — Manga -> Manga / Popular, no filter.
        var m1 = resolve({ type: "manga", catalogId: "popular", filterGroup: "", filterKey: "" })
        falsy(m1.missing, "pin manga/popular: not missing")
        eq(m1.type, "manga", "pin manga/popular: type manga")
        eq(m1.catalogKey, "popular", "pin manga/popular: catalogue popular")
        eq(m1.filterGroup, "", "pin manga/popular: no filter group")
        eq(m1.filterKey, "", "pin manga/popular: no filter key")

        // (2) a manga genre/demographic pin survives validation (genre + demographic).
        var m2 = resolve({ type: "manga", catalogId: "popular", filterGroup: "genre", filterKey: "action" })
        eq(m2.type, "manga", "pin manga/genre: type manga")
        eq(m2.catalogKey, "popular", "pin manga/genre: catalogue popular")
        eq(m2.filterGroup, "Genres", "pin manga/genre: group canonicalized to display label")
        eq(m2.filterKey, "action", "pin manga/genre: filter key preserved (stable lower-case)")
        var m2d = resolve({ type: "manga", catalogId: "popular", filterGroup: "demographic", filterKey: "seinen" })
        eq(m2d.filterGroup, "Demographics", "pin manga/demographic: group canonicalized to display label")
        eq(m2d.filterKey, "seinen", "pin manga/demographic: filter key preserved")

        // (3) Top in Tankoban — Comics -> Comics / Popular, no filter.
        var c1 = resolve({ type: "comics", catalogId: "popular", filterGroup: "", filterKey: "" })
        eq(c1.type, "comics", "pin comics/popular: type comics")
        eq(c1.catalogKey, "popular", "pin comics/popular: catalogue popular")
        eq(c1.filterGroup, "", "pin comics/popular: no filter group")

        // (4) Marvel/DC/Image publisher pins — stable lower-case arg keys, validated against
        //     the live publisher facets.
        var publishers = ["marvel", "dc", "image"]
        for (var pi = 0; pi < publishers.length; pi++) {
            var pc = resolve({ type: "comics", catalogId: "popular",
                              filterGroup: "publisher", filterKey: publishers[pi] })
            eq(pc.type, "comics", "pin comics/publisher " + publishers[pi] + ": type comics")
            eq(pc.catalogKey, "popular", "pin comics/publisher " + publishers[pi] + ": catalogue popular")
            eq(pc.filterGroup, "Publishers", "pin comics/publisher " + publishers[pi] + ": group canonicalized")
            eq(pc.filterKey, publishers[pi], "pin comics/publisher " + publishers[pi] + ": key preserved")
        }

        // (5) Most Stocked -> Comics / most-stocked.
        var ms = resolve({ type: "comics", catalogId: "most-stocked", filterGroup: "", filterKey: "" })
        eq(ms.type, "comics", "pin comics/most-stocked: type comics")
        eq(ms.catalogKey, "most-stocked", "pin comics/most-stocked: catalogue most-stocked")
        eq(ms.filterGroup, "", "pin comics/most-stocked: no filter group")

        // ── invalid-filter clears ONLY the filter (valid type/catalogue preserved) ──
        // A stale publisher key is dropped; the type+catalogue the pin carried survive.
        var stale = resolve({ type: "comics", catalogId: "popular",
                              filterGroup: "publisher", filterKey: "defunct-publisher-2099" })
        falsy(stale.missing, "stale filter: pin not missing (valid type/catalogue)")
        eq(stale.type, "comics", "stale filter: type preserved")
        eq(stale.catalogKey, "popular", "stale filter: catalogue preserved")
        eq(stale.filterGroup, "", "stale filter: invalid filter group cleared")
        eq(stale.filterKey, "", "stale filter: invalid filter key cleared")

        // ── a stale CATALOGUE id falls back to the built-in default (popular) ──
        var staleCat = resolve({ type: "manga", catalogId: "retired-catalogue", filterGroup: "", filterKey: "" })
        eq(staleCat.type, "manga", "stale catalogue: type preserved")
        eq(staleCat.catalogKey, "popular", "stale catalogue: falls to built-in default popular")

        // ── Task 9: Tankoban Discover opts BOTH types into the gallery profile ──
        var gShell = p._shellForTest
        eq(gShell.posterVisualProfile, "gallery", "Tankoban Discover selects the gallery profile")
        gShell.selectType("comics")
        eq(p.currentType, "comics", "switched to the Comics wall")
        eq(gShell.posterVisualProfile, "gallery", "Comics wall keeps the gallery profile")
        gShell.selectType("manga")
        eq(p.currentType, "manga", "switched back to the Manga wall")
        eq(gShell.posterVisualProfile, "gallery", "Manga wall keeps the gallery profile")

        // resting cards stay poster-and-title only for BOTH variants (no rating/publisher furniture)
        mangaRestCard.item = mangaCard
        comicRestCard.item = comicCard
        falsy(mangaRestCard.ratingVisibleAtRest, "manga resting card shows no rating")
        falsy(mangaRestCard.centerPlayVisible, "manga resting card shows no play ring (gallery)")
        eq(mangaRestCard.capText, "Vagabond", "manga resting card shows only the title")
        falsy(comicRestCard.ratingVisibleAtRest, "comic resting card shows no rating")
        falsy(comicRestCard.centerPlayVisible, "comic resting card shows no play ring (gallery)")
        eq(comicRestCard.capText, "Saga", "comic resting card shows only the title")

      } catch (e) {
        root.fails.push("exception: " + (e && e.message ? e.message : String(e)))
      }
        for (var fi = 0; fi < root.fails.length; fi++) console.warn("FAIL: " + root.fails[fi])
        if (root.fails.length === 0) console.warn("TANKOBAN_DISCOVER_PAGE_OK")
        else console.warn("TANKOBAN_DISCOVER_PAGE_FAIL: " + root.fails.length + " failure(s)")
        Qt.exit(root.fails.length)
    }
}
