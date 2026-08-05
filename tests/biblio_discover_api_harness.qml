// Offscreen contract proof for BiblioDiscoverApi.js (Task 5, arc 2026-08-01).
//
// Drives the Biblio adapter through a FAKE BiblioCatalog-shaped object (no real SQLite) and a
// fake extension registry (no real network — extension DESCRIPTOR derivation is pure; the
// network-riding fetchPage extension path is exercised separately, offline-safe, by
// biblio_discover_page_harness.qml against the built-in path only). House rule: NEVER throw
// (hangs offscreen) — collect fails, print BIBLIO_DISCOVER_API_OK only when clean, single
// Qt.exit(fails.length).
import QtQuick
import "../qml/BiblioDiscoverApi.js" as Api
import "../qml/DiscoverApi.js" as DiscoverApi

Item {
    id: root

    property var fails: []
    function fail(m) { root.fails.push(m) }
    function eq(a, b, m) { if (a !== b) fail(m + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")") }
    function truthy(a, m) { if (!a) fail(m) }
    function falsy(a, m) { if (a) fail(m) }

    // ── fake BiblioCatalog ──
    // discoverPage(catalogId, facetAxis, facetKey, includeExplicit, offset, limit) mirrors the
    // native {items,nextOffset,exhausted,freshness,warning} shape EXACTLY (BiblioCatalog.h).
    // filterGroups(includeExplicit) mirrors BiblioCatalogStore::filterGroups's
    // [{axis,label,facets:[{key,label}]}] shape exactly.
    QtObject {
        id: fakeBiblio
        property bool offline: false
        property bool stale: false
        property bool ready: true
        property int pageCalls: 0
        property var groups: [
            { axis: "genre", label: "Genre",
              facets: [{ key: "fiction", label: "Fiction" }, { key: "nonfiction", label: "Nonfiction" }] },
            { axis: "audience", label: "Audience",
              facets: [{ key: "adult", label: "Adult" }, { key: "young-adult", label: "Young Adult" }] }
        ]
        function filterGroups(includeExplicit) { return groups }

        property var popularRows: [
            { canonicalId: "w1", title: "Book One", author: "Author A", originalLanguage: "en",
              canonicalFirstPublished: "2020-05-01", publisher: "Pub A", coverUrl: "https://c/1.jpg",
              rating: { average: 4.5, count: 100 }, score: 10, rank: 1 },
            { canonicalId: "w2", title: "Book Two", author: "Author B", originalLanguage: "en",
              canonicalFirstPublished: "2019-01-01", publisher: "Pub B", coverUrl: "https://c/2.jpg",
              rating: { average: 4.0, count: 50 }, score: 8, rank: 2 }
        ]
        property var fictionRows: [
            { canonicalId: "w1", title: "Book One", author: "Author A", originalLanguage: "en",
              canonicalFirstPublished: "2020-05-01", publisher: "Pub A", coverUrl: "https://c/1.jpg",
              rating: { average: 4.5, count: 100 }, score: 10, rank: 1 }
        ]
        function discoverPage(catalogId, facetAxis, facetKey, includeExplicit, offset, limit) {
            pageCalls++
            var rows = [];
            if (catalogId === "popular") {
                if (facetAxis === "genre" && facetKey === "fiction") rows = fictionRows;
                else rows = popularRows;
            }
            var page = rows.slice(offset, offset + limit)
            return { items: page, nextOffset: offset + page.length,
                     exhausted: page.length < limit, freshness: "fresh", warning: "" }
        }
    }

    Timer {
        interval: 30; running: true; repeat: false
        onTriggered: root.run()
    }

    function run() {
      try {
        // ── normalizeBook: native row -> normalized card ──
        var nb = Api.normalizeBook(fakeBiblio.popularRows[0])
        eq(nb.id, "w1", "normalizeBook id = canonicalId")
        eq(nb.type, "book", "normalizeBook type is book")
        eq(nb.title, "Book One", "normalizeBook title")
        eq(nb.author, "Author A", "normalizeBook author preserved (author-at-rest field)")
        eq(nb.year, 2020, "normalizeBook year parsed from canonicalFirstPublished")
        eq(nb.rating, 4.5, "normalizeBook rating = rating.average")
        eq(nb.publisher, "Pub A", "normalizeBook publisher preserved")
        eq(nb.cover, "https://c/1.jpg", "normalizeBook cover = coverUrl")
        truthy(nb.availability, "normalizeBook availability always true (acquisition never gates discovery)")
        eq(nb.raw.canonicalId, "w1", "normalizeBook keeps the original row in raw")

        // ── adapter factory ──
        var adapter = Api.create(fakeBiblio, [], false)

        // exactly one type: book.
        var types = adapter.types()
        eq(types.length, 1, "adapter types: exactly one")
        eq(types[0].key, "book", "adapter types: book key")

        // exactly FOUR built-ins, in fixed native order.
        var cats = adapter.catalogs("book")
        eq(cats.length, 4, "book catalogs: 4 built-ins (no extensions installed)")
        eq(cats[0].key, "popular", "book catalogs: popular first")
        eq(cats[0].title, "Popular", "book catalogs: popular title")
        eq(cats[1].key, "top-rated", "book catalogs: top-rated second")
        eq(cats[1].title, "Top Rated", "book catalogs: top-rated title")
        eq(cats[2].key, "new-releases", "book catalogs: new-releases third")
        eq(cats[2].title, "New Releases", "book catalogs: new-releases title")
        eq(cats[3].key, "trending", "book catalogs: trending fourth")
        eq(cats[3].title, "Trending", "book catalogs: trending title")
        for (var i = 0; i < cats.length; i++) {
            eq(cats[i].section, "Biblio", "built-in section is Biblio")
            eq(cats[i].attribution, "Biblio built-in catalogue", "built-in attribution")
            eq(cats[i].sourceKind, "builtin", "built-in sourceKind")
        }

        // defaultCatalog: Popular.
        eq(adapter.defaultCatalog("book"), "popular", "defaultCatalog book = popular")

        // ── filters: exact normalized shape, reusing BiblioCatalog.filterGroups() ──
        var filters = adapter.filters("book", "popular")
        eq(filters.length, 2, "book filters: 2 groups (Genre + Audience)")
        eq(filters[0].group, "Genre", "book filter group: Genre")
        eq(filters[0].options.length, 2, "Genre options present")
        eq(filters[0].options[0].key, "fiction", "genre facet key stable lower-case")
        eq(filters[0].options[0].label, "Fiction", "genre facet label preserved")
        eq(filters[1].group, "Audience", "book filter group: Audience")
        eq(filters[1].options[1].key, "young-adult", "audience facet key stable lower-case")

        // ── extension catalogue seam ──
        var extensions = [
            // a valid, non-core, enabled, input-free book extension -> appears under "From Your Extensions".
            { id: "ext.bookhub", enabled: true, core: false, transportUrl: "https://bookhub.example/manifest.json",
              manifest: { id: "ext.bookhub", name: "BookHub",
                          catalogs: [{ type: "book", id: "top", name: "Top Books", isDiscoverable: true, extra: [] }] } },
            // acquisition/download-only well, explicitly marked -> rejected.
            { id: "ext.dl", enabled: true, core: false, transportUrl: "https://dl.example/manifest.json",
              manifest: { id: "ext.dl", name: "Downloader",
                          catalogs: [{ type: "book", id: "dl", name: "Download Wells", isDiscoverable: false }] } },
            // a required, unanswerable extra (search-only) -> rejected (input-free gate).
            { id: "ext.search", enabled: true, core: false, transportUrl: "https://search.example/manifest.json",
              manifest: { id: "ext.search", name: "SearchOnly",
                          catalogs: [{ type: "book", id: "q", name: "Search Books",
                                       extra: [{ name: "search", isRequired: true }] }] } },
            // Apple Books itself (even if it ever appears as an installed, non-core entry) -> rejected.
            { id: "apple-books", enabled: true, core: false, transportUrl: "https://apple.example/manifest.json",
              manifest: { id: "apple-books", name: "Apple Books",
                          catalogs: [{ type: "book", id: "top", name: "Apple Top", isDiscoverable: true }] } },
            // a CORE row -> rejected (core rows are the native source, never a chosen extension).
            { id: "core.books", enabled: true, core: true, transportUrl: "https://core.example/manifest.json",
              manifest: { id: "core.books", name: "CoreBooks",
                          catalogs: [{ type: "book", id: "top", name: "Core Top", isDiscoverable: true } ] } },
            // disabled -> rejected.
            { id: "ext.disabled", enabled: false, core: false, transportUrl: "https://disabled.example/manifest.json",
              manifest: { id: "ext.disabled", name: "Disabled",
                          catalogs: [{ type: "book", id: "x", name: "X", isDiscoverable: true }] } },
            // wrong content type -> rejected.
            { id: "ext.movie", enabled: true, core: false, transportUrl: "https://movie.example/manifest.json",
              manifest: { id: "ext.movie", name: "MovieExt",
                          catalogs: [{ type: "movie", id: "m", name: "Movies", isDiscoverable: true }] } }
        ]
        var extAdapter = Api.create(fakeBiblio, extensions, false)
        var extCats = extAdapter.catalogs("book")
        eq(extCats.length, 5, "extension seam: 4 built-ins + exactly 1 legitimate extension catalogue")
        eq(extCats[4].key, "https://bookhub.example/manifest.json|book|top", "extension key matches DiscoverApi's key format")
        eq(extCats[4].title, "Top Books", "extension title preserved")
        eq(extCats[4].sourceKind, "extension", "extension sourceKind")
        eq(extCats[4].section, "From Your Extensions", "extension section is 'From Your Extensions'")
        eq(extCats[4].attribution, "BookHub", "extension attribution is the manifest name")
        // none of the rejected entries leaked through under any section.
        for (var ci = 0; ci < extCats.length; ci++) {
            falsy(extCats[ci].attribution === "Downloader", "download-only well excluded")
            falsy(extCats[ci].attribution === "SearchOnly", "unanswerable-required-extra catalogue excluded")
            falsy(extCats[ci].attribution === "Apple Books", "Apple Books itself excluded as a browsable extension")
            falsy(extCats[ci].attribution === "CoreBooks", "core row excluded (native source, not a chosen extension)")
            falsy(extCats[ci].attribution === "Disabled", "disabled extension excluded")
            falsy(extCats[ci].attribution === "MovieExt", "non-book catalogue type excluded")
        }

        // ── resolvePin ──
        // a valid catalogue + filter round-trips.
        var rp = adapter.resolvePin({ type: "book", catalogId: "popular", filterGroup: "genre", filterKey: "fiction" })
        falsy(rp.missing, "resolvePin: valid book pin not missing")
        eq(rp.type, "book", "resolvePin: type echoed")
        eq(rp.catalogKey, "popular", "resolvePin: catalogKey resolved")
        eq(rp.filterGroup, "Genre", "resolvePin: filterGroup canonicalized to display label")
        eq(rp.filterKey, "fiction", "resolvePin: filterKey preserved")

        // a stale filter key is dropped while the valid catalogue survives.
        var stale = adapter.resolvePin({ type: "book", catalogId: "popular", filterGroup: "genre", filterKey: "defunct" })
        falsy(stale.missing, "resolvePin stale filter: not missing")
        eq(stale.catalogKey, "popular", "resolvePin stale filter: catalogue preserved")
        eq(stale.filterGroup, "", "resolvePin stale filter: invalid filter group dropped")
        eq(stale.filterKey, "", "resolvePin stale filter: invalid filter key dropped")

        // missing-extension fallback: a pin naming a since-removed extension catalogue falls
        // back to the Popular built-in (not "missing" — Biblio still has its 4 built-ins).
        var staleCat = adapter.resolvePin({ type: "book", catalogId: "https://gone.example/manifest.json|book|old", filterGroup: "", filterKey: "" })
        falsy(staleCat.missing, "resolvePin stale catalogue: not missing (built-ins always exist)")
        eq(staleCat.type, "book", "resolvePin stale catalogue: type preserved")
        eq(staleCat.catalogKey, "popular", "resolvePin stale catalogue: falls back to Popular")

        // a pin naming an unknown type is missing (Biblio has no catalogues for it).
        var unknownType = adapter.resolvePin({ type: "movie", catalogId: "popular" })
        truthy(unknownType.missing, "resolvePin: unknown type is missing")

        // ── fetchPage: local-first, synchronous native call, no XHR indirection ──
        var captured = null
        adapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "", filterKey: "" },
                          null, 1, function(gen, page) { captured = { gen: gen, page: page } })
        truthy(captured !== null, "fetchPage: callback invoked synchronously (native invokable, no XHR)")
        eq(captured.gen, 1, "fetchPage: generation echoed")
        eq(captured.page.items.length, 2, "fetchPage: both popular rows delivered")
        eq(captured.page.items[0].title, "Book One", "fetchPage: item shape carries title")
        eq(captured.page.items[0].author, "Author A", "fetchPage: item shape carries author")
        truthy(captured.page.exhausted, "fetchPage: short page -> exhausted")

        // filter threading: a Fiction genre filter scopes the popular page.
        var filtered = null
        adapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "Genre", filterKey: "fiction" },
                          null, 2, function(gen, page) { filtered = page })
        eq(filtered.items.length, 1, "fetchPage+filter: scoped to 1 row")
        eq(filtered.items[0].title, "Book One", "fetchPage+filter: the Fiction-scoped row")

        // offline: the fake reports offline -> the shell's exact offline notice string surfaces.
        fakeBiblio.offline = true
        var offlineCaptured = null
        adapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "", filterKey: "" },
                          null, 3, function(gen, page) { offlineCaptured = page })
        eq(offlineCaptured.warning, "Showing offline catalogue", "fetchPage offline: exact shell offline-notice text surfaces")
        fakeBiblio.offline = false

        // Task 9 lifecycle: `stale` (a READY cache that just isn't from today — the normal window
        // before the daily refresh completes, BiblioCatalog.cpp's recomputeStale()) is a DIFFERENT
        // signal from `offline` (the catalogue could not be refreshed at all). Only `offline` is
        // wired to a banner (BiblioDiscoverApi.js's fetchBuiltinPage reads bc.offline, never
        // bc.stale) — a bare `stale` must NOT surface any warning, since it fires on every normal
        // cold start and would be a false alarm, not an honest "you're seeing old data" notice.
        fakeBiblio.stale = true
        var staleCaptured = null
        adapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "", filterKey: "" },
                          null, 10, function(gen, page) { staleCaptured = page })
        eq(staleCaptured.warning, "", "fetchPage stale-but-online: no warning surfaces (stale alone is not offline)")
        fakeBiblio.stale = false

        // Task 9 lifecycle: no-cache first-sync (BiblioCatalog.ready has never gone true — the
        // store has no published snapshot at all). discoverPage() is a bare proxy onto the store
        // (native does not gate it on `ready`); a store with nothing yet returns an honest empty,
        // exhausted page — never a crash and never confused with the offline warning.
        fakeBiblio.ready = false
        var noCacheCaptured = null
        adapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "", filterKey: "" },
                          null, 11, function(gen, page) { noCacheCaptured = page })
        truthy(noCacheCaptured !== null, "fetchPage no-cache: never throws before the first successful sync")
        eq(noCacheCaptured.items.length, 2, "fetchPage no-cache: the fake's discoverPage is a bare store proxy regardless of ready, so an already-published dataset still answers honestly")
        eq(noCacheCaptured.warning, "", "fetchPage no-cache: not confused with the offline warning")
        fakeBiblio.ready = true

        // ── unsupported extension filters: a filter group/key that doesn't exist on a GIVEN
        // extension catalogue (its OWN manifest extras) must be silently dropped, never crash and
        // never leak a bogus selection into the fetch — selectionsForFilter degrades to the
        // catalogue's default/required selections only. ──
        var narrowCatalog = { transportUrl: "https://narrow.example/manifest.json", type: "book", id: "top",
                               extra: [{ name: "skip", isRequired: false }] }
        var bogusSel = DiscoverApi.selectionsForFilter(narrowCatalog, "Audience", "adult")
        truthy(bogusSel !== null && typeof bogusSel === "object",
               "selectionsForFilter: an unsupported filter group never throws, still returns a selections object")
        falsy(Object.prototype.hasOwnProperty.call(bogusSel, "Audience"),
              "selectionsForFilter: the unsupported group name never leaks into the selections map, got " + JSON.stringify(bogusSel))
        falsy(Object.prototype.hasOwnProperty.call(bogusSel, "adult"),
              "selectionsForFilter: the unsupported filter key never leaks into the selections map, got " + JSON.stringify(bogusSel))
        var requiredCatalog = { transportUrl: "https://req.example/manifest.json", type: "book", id: "top",
                                 extra: [{ name: "genre", label: "Genre", isRequired: true, options: ["fiction"] }] }
        var bogusSel2 = DiscoverApi.selectionsForFilter(requiredCatalog, "Audience", "adult")
        truthy(bogusSel2.genre !== undefined && bogusSel2.genre !== null,
               "selectionsForFilter: a REQUIRED extra keeps its auto-picked default even when the active filter names an unrelated group, got " + JSON.stringify(bogusSel2))

        // a missing biblioCatalog is null-safe.
        var bareAdapter = Api.create(null, [], false)
        var barePage = null
        bareAdapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "", filterKey: "" },
                              null, 4, function(gen, page) { barePage = page })
        eq(barePage.items.length, 0, "fetchPage: null biblioCatalog -> empty page, never throws")
        truthy(barePage.exhausted, "fetchPage: null biblioCatalog -> exhausted")

        var src = Api.SOURCE
        truthy(src, "adapter exposes SOURCE for static contract")

      } catch (e) {
        root.fails.push("exception: " + (e && e.message ? e.message : String(e)))
      }
        for (var fi = 0; fi < root.fails.length; fi++) console.warn("FAIL: " + root.fails[fi])
        if (root.fails.length === 0) console.warn("BIBLIO_DISCOVER_API_OK")
        else console.warn("BIBLIO_DISCOVER_API_FAIL: " + root.fails.length + " failure(s)")
        Qt.exit(root.fails.length)
    }
}
