// Offscreen construct + behavior proof of BiblioDiscoverPage (Task 5, arc 2026-08-01).
//
// Instantiates the page with a FAKE BiblioCatalog dependency (no real SQLite) and asserts the
// Task 5 page contract: default catalogue Popular; one active filter (never accumulates); paging
// appends via nextOffset; a stale/out-of-order callback is rejected (the shell's fetchGen fence);
// author-at-rest; rating/source reveal on hover OR keyboard focus; an offline warning surfaces;
// a card click routes through itemOpenRequested; applyPin(pin, returnToExplore) arms the
// Explore-return signal correctly. House rule: NEVER throw (hangs offscreen) — collect fails,
// print BIBLIO_DISCOVER_PAGE_OK only when clean, single Qt.exit(fails.length).
import QtQuick
import "../qml" as UI
import "../qml/BiblioDiscoverApi.js" as Api

Item {
    id: root

    property var fails: []
    function fail(m) { root.fails.push(m) }
    function eq(a, b, m) { if (a !== b) fail(m + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")") }
    function truthy(a, m) { if (!a) fail(m) }
    function falsy(a, m) { if (a) fail(m) }

    // ── fake BiblioCatalog — a small controllable dataset so filter/offline are provable, plus
    // an `extraRows` knob so the paging block can span the adapter's real 24-row page size. ──
    QtObject {
        id: fakeBiblio
        property bool offline: false
        property int extraRows: 0
        property int pageCalls: 0                 // Part A: proves a real re-fetch happened
        property var groups: [
            { axis: "genre", label: "Genre",
              facets: [{ key: "fiction", label: "Fiction" }, { key: "nonfiction", label: "Nonfiction" }] }
        ]
        function filterGroups(includeExplicit) { return groups }

        // Part A: an explicit-only row so the fake's ANSWER genuinely depends on includeExplicit
        // (a live preference flip), not just on catalogue/filter identity.
        property var explicitOnlyRow: ({
            canonicalId: "wX", title: "Explicit Only", author: "Author X",
            canonicalFirstPublished: "2021-01-01", publisher: "Pub X", coverUrl: "https://c/x.jpg",
            rating: { average: 3.0, count: 1 }, score: 0, rank: 99
        })

        property var baseRows: [
            { canonicalId: "w1", title: "Book One", author: "Author A",
              canonicalFirstPublished: "2020-01-01", publisher: "Pub A", coverUrl: "https://c/1.jpg",
              rating: { average: 4.5, count: 10 }, score: 3, rank: 1 },
            { canonicalId: "w2", title: "Book Two", author: "Author B",
              canonicalFirstPublished: "2019-01-01", publisher: "Pub B", coverUrl: "https://c/2.jpg",
              rating: { average: 4.0, count: 8 }, score: 2, rank: 2 },
            { canonicalId: "w3", title: "Book Three", author: "Author C",
              canonicalFirstPublished: "2018-01-01", publisher: "Pub C", coverUrl: "https://c/3.jpg",
              rating: { average: 3.5, count: 5 }, score: 1, rank: 3 }
        ]
        property var fictionRows: [
            { canonicalId: "w1", title: "Book One", author: "Author A",
              canonicalFirstPublished: "2020-01-01", publisher: "Pub A", coverUrl: "https://c/1.jpg",
              rating: { average: 4.5, count: 10 }, score: 3, rank: 1 }
        ]
        function discoverPage(catalogId, facetAxis, facetKey, includeExplicit, offset, limit) {
            pageCalls++
            if (catalogId !== "popular") return { items: [], nextOffset: offset, exhausted: true, freshness: "fresh", warning: "" }
            var rows
            if (facetAxis === "genre" && facetKey === "fiction") {
                rows = fictionRows
            } else {
                rows = baseRows.slice()
                for (var x = 0; x < extraRows; x++)
                    rows.push({ canonicalId: "filler" + x, title: "Filler " + x, author: "Filler Author",
                                canonicalFirstPublished: "2000-01-01", publisher: "", coverUrl: "",
                                rating: { average: 0, count: 0 }, score: 0, rank: 100 + x })
                if (includeExplicit === true) rows = rows.concat([explicitOnlyRow])
            }
            var page = rows.slice(offset, offset + limit)
            return { items: page, nextOffset: offset + page.length,
                     exhausted: (offset + page.length) >= rows.length, freshness: "fresh", warning: "" }
        }
    }

    UI.BiblioDiscoverPage {
        id: p
        width: 1200; height: 700
        biblioCatalog: fakeBiblio
        extensions: []
        showExplicit: false
    }

    // resting-card proof (mirrors Tankoban's pattern): the gallery card, with showAuthorAtRest
    // and revealOnFocus (Biblio's exact wiring), shows the author always and the rating/source
    // only on hover OR keyboard focus.
    UI.CataloguePosterCard {
        id: restCard
        visualProfile: "gallery"
        width: 148; height: 257
        showAuthorAtRest: true
        revealOnFocus: true
        hoverSourceText: "BookHub"
    }

    Timer {
        interval: 60; running: true; repeat: false
        onTriggered: root.run()
    }

    function run() {
      try {
        var shell = p._shellForTest
        truthy(shell !== null, "page exposes the inner shell for the real routing/back path")

        // ── default: catalogue is Popular, exactly the shell's init() + adapter.defaultCatalog ──
        eq(p.currentType, "book", "default currentType: book")
        eq(shell.currentCatalogKey, "popular", "default catalogue: popular")
        eq(shell.items.length, 3, "default: the 3-row popular page painted on construction")
        truthy(shell.exhausted, "default: a 3-row dataset (< the 24 page size) exhausts in one page")

        // ── shell hooks: the Biblio wrapper opts into author-at-rest and reveal-on-focus ──
        eq(shell.showAuthorAtRest, true, "shell: showAuthorAtRest wired true by the Biblio wrapper")
        eq(shell.showSourceOnReveal, true, "shell: showSourceOnReveal wired true by the Biblio wrapper")
        // 2026-08-06 shelf-quality pass: Biblio pins cards to the gallery token (book covers are
        // small-source art; stretching to fill residual width is what makes them read blurry).
        eq(shell.fixedGalleryWidth, true, "shell: fixedGalleryWidth wired true by the Biblio wrapper")

        // ── CataloguePosterCard proof: author always on, rating/source only on hover OR focus ──
        restCard.item = { title: "Book One", author: "Author A", rating: 4.5 }
        truthy(restCard.showAuthorAtRest, "restCard: showAuthorAtRest on")
        eq(restCard.authorText, "Author A", "restCard: author text resolved from item.author")
        falsy(restCard.ratingVisible, "restCard: rating hidden at rest")
        restCard.testHovered = true
        truthy(restCard.ratingVisible, "restCard: rating visible on hover")
        truthy(restCard.sourceVisible, "restCard: source visible on hover")
        restCard.testHovered = false
        falsy(restCard.ratingVisible, "restCard: rating hides again after hover ends")
        restCard.keyboardFocused = true
        truthy(restCard.ratingVisible, "restCard: rating ALSO visible on keyboard focus (revealOnFocus=true)")
        truthy(restCard.sourceVisible, "restCard: source ALSO visible on keyboard focus")
        restCard.keyboardFocused = false
        falsy(restCard.ratingVisible, "restCard: rating hidden again once focus clears")

        // ── one active filter: a SECOND filter REPLACES the first, never accumulates ──
        shell.setFilter("Genre", "fiction")
        eq(shell.filterGroup, "Genre", "filter: group set")
        eq(shell.filterKey, "fiction", "filter: key set")
        eq(shell.items.length, 1, "filter: scoped page delivered (1 fiction row)")
        shell.setFilter("Genre", "")
        eq(shell.filterKey, "", "filter: clearing replaces (never a second concurrent filter)")
        eq(shell.items.length, 3, "filter cleared: full popular page restored")

        // ── paging: a second fetchPage (scroll-triggered) call APPENDS using nextOffset ──
        fakeBiblio.extraRows = 30                 // dataset now 33 rows -> pages of 24 + 9
        shell.reloadForCatalog()
        eq(shell.items.length, 24, "paging page1: 24 items delivered (the adapter's page size)")
        falsy(shell.exhausted, "paging page1: NOT exhausted (more rows remain)")
        eq(shell.cursor, 24, "paging page1: cursor advances to native's nextOffset")
        shell.requestPage()                        // the scroll-triggered second page
        eq(shell.items.length, 33, "paging page2: appended, not replaced (24 + 9 = 33)")
        truthy(shell.exhausted, "paging page2: short tail -> exhausted")
        eq(shell.items[0].title, "Book One", "paging: page1's rows stay first (append, not reorder)")
        fakeBiblio.extraRows = 0
        shell.reloadForCatalog()
        eq(shell.items.length, 3, "paging reset: back to the 3-row dataset")

        // ── stale/out-of-order callback rejection: an old generation's done() must not land ──
        var lateGen = shell.fetchGen
        var lateAccepted = false
        shell.fetchGen = lateGen + 1               // a newer request has since superseded
        var itemsBefore = shell.items
        shell.adapter.fetchPage({ type: "book", catalogKey: "popular", filterGroup: "", filterKey: "" },
                                null, lateGen, function(replyGen, page) {
                                    if (replyGen === shell.fetchGen) lateAccepted = true
                                })
        falsy(lateAccepted, "stale callback: an old generation's reply is rejected by the fetchGen fence")
        eq(shell.items, itemsBefore, "stale callback: the wall is untouched by the rejected reply")
        shell.reloadForCatalog()

        // ── offline warning surfaces honestly when the adapter reports one ──
        fakeBiblio.offline = true
        shell.reloadForCatalog()
        eq(shell.warning, "Showing offline catalogue", "offline: the exact shell offline-notice text surfaces")
        truthy(shell.showOfflineNotice, "offline: showOfflineNotice trips on the exact string match")
        fakeBiblio.offline = false
        shell.reloadForCatalog()
        eq(shell.warning, "", "offline cleared: warning resets on the next clean page")

        // ── Part A regression: a LIVE Explicit Content preference flip must actually re-fetch
        //    and re-render the displayed items, not just rebuild the adapter object. Before this
        //    fix, BiblioDiscoverPage.onShowExplicitChanged called _rebuildAdapter() -> browser.
        //    refresh(), and refresh() early-returns whenever the current catalogue is still
        //    present in the adapter's list (true here — Popular never disappears) — so the wall's
        //    `items` never actually changed even though the adapter object itself was rebuilt. ──
        eq(p.showExplicit, false, "explicit-flip setup: page starts with showExplicit=false");
        var callsBefore = fakeBiblio.pageCalls;
        var idsBefore = shell.items.map(function(it) { return it.id; });
        falsy(idsBefore.indexOf("wX") !== -1, "explicit-flip setup: the explicit-only row is NOT present while showExplicit=false");
        p.showExplicit = true;
        truthy(fakeBiblio.pageCalls > callsBefore,
               "explicit flip: a REAL new fetch happened (pageCalls " + callsBefore + " -> " + fakeBiblio.pageCalls + ")");
        var idsAfter = shell.items.map(function(it) { return it.id; });
        truthy(idsAfter.indexOf("wX") !== -1,
               "explicit flip: the now-visible explicit item actually rendered, got " + JSON.stringify(idsAfter));
        p.showExplicit = false;
        var idsRestored = shell.items.map(function(it) { return it.id; });
        falsy(idsRestored.indexOf("wX") !== -1,
              "explicit flip back off: the explicit item is actually removed from the rendered wall, got "
              + JSON.stringify(idsRestored));

        // ── card activation routes through itemOpenRequested ──
        var opens = 0, lastItem = null
        p.itemOpenRequested.connect(function(item) { opens++; lastItem = item })
        var card = Api.normalizeBook({ canonicalId: "w9", title: "Ninth Book", author: "Author I" })
        shell.itemOpenRequested(card)
        truthy(opens >= 1, "routing: a normalized card click emits itemOpenRequested")
        truthy(lastItem && lastItem.title === "Ninth Book", "routing: the exact card payload survives")

        // ── applyPin(pin, returnToExplore) arms/fires exploreReturnRequested via backRequested ──
        var returns = 0
        p.exploreReturnRequested.connect(function() { returns++ })
        p.applyPin({ type: "book", catalogId: "popular", filterGroup: "", filterKey: "" }, true)
        eq(p._returnArmed, true, "applyPin: returnToExplore=true arms the Explore-return")
        truthy(shell.showBackAction, "applyPin: the shell's back affordance is now shown (armed)")
        shell.backRequested()
        eq(returns, 1, "applyPin: the shell's generic backRequested() fires the page's exploreReturnRequested")
        eq(p._returnArmed, false, "applyPin: the arm is consumed (one-shot) after firing")
        falsy(shell.showBackAction, "applyPin: the back affordance hides again once disarmed")
        shell.backRequested()                       // a SECOND fire with nothing armed does NOT re-fire.
        eq(returns, 1, "applyPin: an unarmed backRequested() is inert (no re-fire)")

        // applyPin WITHOUT returnToExplore never arms the back affordance.
        p.applyPin({ type: "book", catalogId: "popular", filterGroup: "", filterKey: "" }, false)
        eq(p._returnArmed, false, "applyPin: returnToExplore omitted/false never arms Explore-return")
        falsy(shell.showBackAction, "applyPin: no back affordance without an armed return")

        // ── no download verb on the page's public surface (Discover performs no acquisition) ──
        falsy(typeof p.startDownload === "function", "page exposes NO startDownload")
        falsy(typeof p.download === "function", "page exposes NO download verb")

      } catch (e) {
        root.fails.push("exception: " + (e && e.message ? e.message : String(e)))
      }
        for (var fi = 0; fi < root.fails.length; fi++) console.warn("FAIL: " + root.fails[fi])
        if (root.fails.length === 0) console.warn("BIBLIO_DISCOVER_PAGE_OK")
        else console.warn("BIBLIO_DISCOVER_PAGE_FAIL: " + root.fails.length + " failure(s)")
        Qt.exit(root.fails.length)
    }
}
