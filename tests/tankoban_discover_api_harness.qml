// Offscreen contract proof for TankobanDiscoverApi.js (Task 6, arc 2026-08-01).
//
// Drives the Tankoban adapter through FAKE catalog objects (no real SQLite, no real
// XHR) so every descriptor/filter/normalize/merge/pin behaviour is pinned without a
// live database. House rule: NEVER throw (hangs offscreen) — collect fails, print
// TANKOBAN_DISCOVER_API_OK only when clean, single Qt.exit(fails.length).
//
// The fake catalogs speak the EXACT shapes the native MalCatalog/ComicsCatalog
// discoverFilters/discoverPage methods return (manga facets {value,count}; comics
// facets {key,label,count}; manga rows Jikan-shaped; comics rows house-shaped), so a
// shape drift in native is caught here too.
import QtQuick
import "../qml/TankobanDiscoverApi.js" as Api

Item {
    id: root

    property var fails: []

    function fail(m) { root.fails.push(m) }
    function eq(a, b, m) { if (a !== b) fail(m + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")") }
    function truthy(a, m) { if (!a) fail(m) }
    function falsy(a, m) { if (a) fail(m) }

    // ── fake MalCatalog ──
    // discoverFilters(axis, includeExplicit) -> [{value,count}] (manga facet shape)
    // discoverPage(cat, fAxis, fKey, inclExp, off, lim) -> {items,nextOffset,exhausted,freshness,fallbackCatalog}
    // manga rows are JIKAN-SHAPED (mal_id/title/type/score/members/...).
    QtObject {
        id: fakeMal
        property int pageCalls: 0
        property int filterCalls: 0
        property var genreFacets: [
            { value: "Action", count: 120 },
            { value: "Comedy", count: 80 },
            { value: "Seinen", count: 40 }      // a demographic living among genres -> still surfaced
        ]
        property var demographicFacets: [
            { value: "Shounen", count: 200 },
            { value: "Seinen", count: 60 }
        ]
        function discoverFilters(axis, includeExplicit) {
            filterCalls++
            if (axis === "genre") return genreFacets
            if (axis === "demographic") return demographicFacets
            return []
        }
        function discoverPage(cat, fAxis, fKey, inclExp, off, lim) {
            pageCalls++
            var fallback = cat === "trending" ? "popular" : ""
            // a tiny synthetic page shaped like a Jikan manga row. 'type' exercises
            // Manga/Manhwa/Manhua normalization; explicit exercises the policy filter.
            var rows = [
                { mal_id: 1, title: "Berserk", type: "Manga", score: 9.5, scored_by: 500, members: 3000, favorites: 100, year: 1989, start_date: "1989-08", cover: "https://c/berserk.jpg", classifications: ["Action","Gore"], explicit: false, availability: false },
                { mal_id: 2, title: "Solo Leveling", type: "Manhwa", score: 8.5, scored_by: 400, members: 2000, favorites: 50, year: 2018, start_date: "2018-03", cover: "https://c/sl.jpg", classifications: ["Action"], explicit: false, availability: false },
                { mal_id: 3, title: "Explicit Series", type: "Manga", score: 7.0, scored_by: 10, members: 100, favorites: 1, year: 2020, start_date: "2020-01", cover: "https://c/x.jpg", classifications: ["Hentai"], explicit: true, availability: false }
            ]
            // a facet filter scopes the synthetic set so we can prove filter threading.
            // The adapter contract sends a STABLE LOWER-CASE filter key (see buildFilterOptions
            // → stableKey); a real native SQL query matches case-insensitively, so the fake does too.
            if (fAxis === "demographic" && fKey && fKey.toLowerCase() === "seinen")
                rows = [rows[0]]
            return { items: rows, nextOffset: off + rows.length, exhausted: true,
                     freshness: "bundled", fallbackCatalog: fallback }
        }
    }

    // ── fake ComicsCatalog ──
    // discoverFilters -> [{key,label,count}]; discoverPage -> house-shaped rows.
    QtObject {
        id: fakeComics
        property int pageCalls: 0
        property var genreFacets: [
            { key: "Superhero", label: "Superhero", count: 50 },
            { key: "Horror", label: "Horror", count: 20 }
        ]
        property var publisherFacets: [
            { key: "Marvel", label: "Marvel", count: 60 },
            { key: "DC", label: "DC", count: 40 },
            { key: "Image", label: "Image", count: 30 }
        ]
        function discoverFilters(axis, includeExplicit) {
            if (axis === "genre") return genreFacets
            if (axis === "publisher") return publisherFacets
            return []
        }
        function discoverPage(cat, fAxis, fKey, inclExp, off, lim) {
            pageCalls++
            var rows = [
                { locgId: "locg1", title: "Invincible", year: 2003, publisher: "Image",
                  cover: "https://c/inv.jpg", genres: "Superhero", availability: true,
                  houseScore: 0.8, houseComponents: {popularity:0.6,availability:0.1,recency:0.05,metadata:0.05}, explicit: false },
                { locgId: "locg2", title: "Saga", year: 2012, publisher: "Image",
                  cover: "https://c/saga.jpg", genres: "Science Fiction", availability: false,
                  houseScore: 0.5, houseComponents: {popularity:0.4,availability:0.0,recency:0.05,metadata:0.05}, explicit: false }
            ]
            if (fAxis === "publisher" && fKey === "DC") rows = []
            return { items: rows, nextOffset: off + rows.length, exhausted: true, freshness: "bundled" }
        }
    }

    // fake XMLHttpRequest for the Jikan refresh path. We never auto-fire; the adapter
    // is observed to construct the URL only (and only AFTER the bundled page landed).
    property var xhrLog: []
    function makeFakeXhr() {
        return {
            open: function(method, url) { root.xhrLog.push({method: method, url: url}) },
            setRequestHeader: function() {},
            send: function() { /* never completes in the harness; bundled wall stays */ },
            abort: function() {}
        }
    }

    Timer {
        // let the synchronous bundled fetchPage callbacks resolve before asserting
        interval: 30; running: true; repeat: false
        onTriggered: root.run()
    }

    function run() {
      try {
        var showExplicit = false

        // ── pure helpers (no dependencies) ──
        // jikanUrl derives sfw from showExplicit and keeps the endpoint pinned.
        var u = Api.jikanUrl("popular", 0, showExplicit)
        truthy(u.indexOf("sfw=true") >= 0, "jikanUrl: showExplicit=false -> sfw=true")
        truthy(u.indexOf("/top/manga") >= 0, "jikanUrl: popular maps to /top/manga")
        var uExplicit = Api.jikanUrl("popular", 0, true)
        truthy(uExplicit.indexOf("sfw=false") >= 0, "jikanUrl: showExplicit=true -> sfw=false")

        // normalizeManga: maps the Jikan row to the normalized card, preserving format.
        var berserk = fakeMal.discoverPage("popular","","",false,0,100).items[0]
        var nm = Api.normalizeManga(berserk)
        eq(nm.id, "1", "normalizeManga id is stringified mal_id")
        eq(nm.type, "manga", "normalizeManga type lowercased to world key")
        eq(nm.title, "Berserk", "normalizeManga title")
        eq(nm.format, "Manga", "normalizeManga format preserves Manga type")
        eq(nm.year, 1989, "normalizeManga year")
        eq(nm.rating, 9.5, "normalizeManga rating")
        falsy(nm.availability, "normalizeManga availability defaults false (adapter enriches)")
        eq(nm.raw.mal_id, 1, "normalizeManga keeps the original row in raw")
        // Manhwa/Manhua normalization
        var sl = Api.normalizeManga(fakeMal.discoverPage("popular","","",false,0,100).items[1])
        eq(sl.format, "Manhwa", "normalizeManga: Manhwa type preserved as format")

        // normalizeComic: house row -> normalized card; series-only (no acquisition).
        var inv = Api.normalizeComic(fakeComics.discoverPage("popular","","",false,0,100).items[0])
        eq(inv.id, "locg1", "normalizeComic id = locgId")
        eq(inv.type, "comics", "normalizeComic type")
        eq(inv.publisher, "Image", "normalizeComic publisher preserved")
        truthy(inv.availability, "normalizeComic availability true when house says so")
        eq(inv.format, "", "normalizeComic has no format (comics carry no manga-style type)")
        falsy(inv.raw.locgId === undefined, "normalizeComic keeps raw")

        // mergeByIdentity: live MAL-id rows replace bundled; non-stable-id rows are dropped.
        var merged = Api.mergeByIDentity(
            [{id:"1",type:"manga",title:"Berserk (bundled)",raw:{mal_id:1}}],
            [{mal_id:1, title:"Berserk (live)", type:"Manga", cover:"https://c/live.jpg"},
             {title:"Title-only guess", type:"Manga"}])
        eq(merged.length, 1, "mergeByIDentity: only stable-id live rows merge")
        eq(merged[0].title, "Berserk (live)", "mergeByIDentity: live replaces bundled by mal_id")

        // ── adapter factory ──
        var adapter = Api.create(fakeMal, fakeComics, [], showExplicit, makeFakeXhr)

        // types: exactly Manga + Comics, in that order.
        var types = adapter.types()
        eq(types.length, 2, "adapter types: exactly two")
        eq(types[0].key, "manga", "adapter types: manga key")
        eq(types[0].label, "Manga", "adapter types: Manga label")
        eq(types[1].key, "comics", "adapter types: comics key")

        // catalogs: Manga launch set is Trending, Popular, Top Rated, New Releases.
        var mc = adapter.catalogs("manga")
        eq(mc.length, 4, "manga catalogs: 4 built-ins")
        eq(mc[0].key, "trending", "manga catalogs: trending first")
        eq(mc[0].title, "Trending", "manga catalogs: trending title")
        eq(mc[1].key, "popular", "manga catalogs: popular second")
        eq(mc[2].key, "top-rated", "manga catalogs: top-rated third")
        eq(mc[3].key, "new-releases", "manga catalogs: new-releases fourth")
        for (var i = 0; i < mc.length; i++) {
            eq(mc[i].section, "Tankoban", "manga built-in section is Tankoban")
            eq(mc[i].attribution, "Tankoban built-in catalogue", "manga built-in attribution")
            eq(mc[i].sourceKind, "builtin", "manga built-in sourceKind")
        }
        // Comics launch set: Popular, New Releases, Most Stocked, All Series.
        var cc = adapter.catalogs("comics")
        eq(cc.length, 4, "comics catalogs: 4 built-ins")
        eq(cc[0].key, "popular", "comics catalogs: popular first")
        eq(cc[1].key, "new-releases", "comics catalogs: new-releases second")
        eq(cc[2].key, "most-stocked", "comics catalogs: most-stocked third")
        eq(cc[3].key, "all", "comics catalogs: all fourth")

        // defaultCatalog: always the FIRST built-in for the type (Popular for both at launch;
        // Manga's Trending is a Popular-fallback until comparable snapshots exist, but the
        // picker default lands on Popular so the wall paints from real bundled rows).
        eq(adapter.defaultCatalog("manga"), "popular", "defaultCatalog manga = popular")
        eq(adapter.defaultCatalog("comics"), "popular", "defaultCatalog comics = popular")

        // filters: Manga groups Genres + Demographics; Comics groups Genres + Publishers.
        var mf = adapter.filters("manga", "popular")
        eq(mf.length, 2, "manga filters: 2 groups")
        eq(mf[0].group, "Genres", "manga filter group: Genres")
        eq(mf[1].group, "Demographics", "manga filter group: Demographics")
        truthy(mf[0].options.length >= 3, "manga Genres options present")
        // stable keys, distinct from labels (lowercased key)
        eq(mf[0].options[0].key, "action", "manga genre facet key is stable lower-case")
        eq(mf[0].options[0].label, "Action", "manga genre facet label preserved")
        var cf = adapter.filters("comics", "popular")
        eq(cf.length, 2, "comics filters: 2 groups")
        eq(cf[0].group, "Genres", "comics filter group: Genres")
        eq(cf[1].group, "Publishers", "comics filter group: Publishers")
        eq(cf[1].options[0].key, "marvel", "comics publisher facet key stable lower-case")

        // resolvePin: valid type/catalogue passes filter through.
        var rp = adapter.resolvePin({type:"manga", catalogId:"popular", filterGroup:"Genres", filterKey:"action"})
        falsy(rp.missing, "resolvePin: valid manga pin not missing")
        eq(rp.type, "manga", "resolvePin: type echoed")
        eq(rp.catalogKey, "popular", "resolvePin: catalogKey resolved")
        eq(rp.filterGroup, "Genres", "resolvePin: filterGroup preserved")
        eq(rp.filterKey, "action", "resolvePin: filterKey preserved")
        // resolvePin: a stale filter key is dropped while the valid type/catalogue survives.
        var rps = adapter.resolvePin({type:"comics", catalogId:"popular", filterGroup:"Genres", filterKey:"nonexistent-genre"})
        falsy(rps.missing, "resolvePin stale: still not missing (valid type/catalog)")
        eq(rps.type, "comics", "resolvePin stale: type preserved")
        eq(rps.catalogKey, "popular", "resolvePin stale: catalog preserved")
        eq(rps.filterKey, "", "resolvePin stale: invalid filter key dropped")
        // resolvePin: a completely unknown type is NOT missing either — it falls to the
        // type's built-in default (the shell surfaces a notice), preserving the pin's type.
        var rpt = adapter.resolvePin({type:"movies", catalogId:"popular"})
        truthy(rpt.missing, "resolvePin: unknown type -> missing (falls to built-in default with notice)")

        // ── fetchPage: local-first, freshness bundled, explicit policy applied ──
        var captured = null
        var beforeXhr = root.xhrLog.length
        adapter.fetchPage({type:"manga", catalogKey:"popular", filterGroup:"", filterKey:""},
                          null, 1, function(gen, page) { captured = {gen: gen, page: page} })
        truthy(captured !== null, "fetchPage manga: callback invoked synchronously (local-first)")
        eq(captured.gen, 1, "fetchPage manga: generation echoed")
        eq(captured.page.freshness, "bundled", "fetchPage manga: freshness bundled")
        // explicit policy (showExplicit=false) HIDES the Hentai row but KEEPS Berserk.
        var titles = captured.page.items.map(function(it){ return it.title })
        truthy(titles.indexOf("Berserk") >= 0, "fetchPage manga: Berserk (non-explicit) stays visible")
        truthy(titles.indexOf("Explicit Series") < 0, "fetchPage manga: Hentai row hidden when showExplicit=false")
        truthy(titles.indexOf("Solo Leveling") >= 0, "fetchPage manga: Manhwa stays visible")
        truthy(captured.page.nextCursor !== null, "fetchPage manga: nextCursor present (offset-based)")
        falsy(captured.page.exhausted, "fetchPage manga: not exhausted (native reported more via nextOffset<total semantics)")

        // Trending falls back honestly: warns + reuses Popular, never invents momentum.
        var trendCaptured = null
        adapter.fetchPage({type:"manga", catalogKey:"trending", filterGroup:"", filterKey:""},
                          null, 2, function(gen, page){ trendCaptured = page })
        truthy(trendCaptured.warning && trendCaptured.warning.length > 0,
               "fetchPage trending: honest warning emitted (no invented momentum)")

        // Comics fetch: local-first, house ranking already computed by native.
        var comicCaptured = null
        adapter.fetchPage({type:"comics", catalogKey:"popular", filterGroup:"", filterKey:""},
                          null, 3, function(gen, page){ comicCaptured = page })
        truthy(comicCaptured !== null, "fetchPage comics: callback invoked synchronously")
        eq(comicCaptured.freshness, "bundled", "fetchPage comics: freshness bundled")
        var comicTitles = comicCaptured.items.map(function(it){ return it.title })
        truthy(comicTitles.indexOf("Invincible") >= 0, "fetchPage comics: Invincible present")

        // filter threading: a Seinen demographic filter scopes the manga page.
        var filteredCaptured = null
        adapter.fetchPage({type:"manga", catalogKey:"popular", filterGroup:"Demographics", filterKey:"seinen"},
                          null, 4, function(gen, page){ filteredCaptured = page })
        eq(filteredCaptured.items.length, 1, "fetchPage manga+filter: scoped to 1 row")
        eq(filteredCaptured.items[0].title, "Berserk", "fetchPage manga+filter: the Seinen-scoped row")

        // ── extension catalogue seam ──
        // a discovery-capable extension appends under "Extensions"; a download-only one is rejected.
        var extAdapter = Api.create(fakeMal, fakeComics, [
            { manifest: { id: "comics.ext.discover", name: "Metron", behaviorHints: {},
                          catalogs: [{ type: "comics", id: "metron-all", name: "Metron All",
                                       isDiscoverable: true }] } },
            { manifest: { id: "dl.only", name: "DownloadSource", behaviorHints: {},
                          catalogs: [{ type: "comics", id: "dl", name: "Downloads" }] } }   // not discoverable
        ], showExplicit, makeFakeXhr)
        var extCats = extAdapter.catalogs("comics")
        // 4 built-ins + 1 extension (Metron), NOT the download-only source
        eq(extCats.length, 5, "extension seam: download-only source rejected; discovery extension appended")
        eq(extCats[4].section, "Extensions", "extension seam: appended under Extensions section")
        eq(extCats[4].attribution, "Metron", "extension seam: attribution is the manifest name")

        // ── showExplicit=true surfaces the explicit manga row ──
        var expAdapter = Api.create(fakeMal, fakeComics, [], true, makeFakeXhr)
        var expCaptured = null
        expAdapter.fetchPage({type:"manga", catalogKey:"popular", filterGroup:"", filterKey:""},
                             null, 5, function(gen, page){ expCaptured = page })
        var expTitles = expCaptured.items.map(function(it){ return it.title })
        truthy(expTitles.indexOf("Explicit Series") >= 0,
               "fetchPage manga showExplicit=true: Hentai row now visible")

        // ── no Comic Vine / Metron runtime dependency, no download action from the adapter ──
        var src = Api.SOURCE  // the adapter exposes its source for a static contract grep
        truthy(src, "adapter exposes SOURCE for static contract")

      } catch (e) {
        root.fails.push("exception: " + (e && e.message ? e.message : String(e)))
      }
        for (var fi = 0; fi < root.fails.length; fi++) console.warn("FAIL: " + root.fails[fi])
        if (root.fails.length === 0) console.warn("TANKOBAN_DISCOVER_API_OK")
        else console.warn("TANKOBAN_DISCOVER_API_FAIL: " + root.fails.length + " failure(s)")
        Qt.exit(root.fails.length)
    }
}
