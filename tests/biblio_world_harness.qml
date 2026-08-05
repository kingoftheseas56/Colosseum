// Offscreen construct + wiring proof of BiblioWorld (plan `2026-08-01-biblio-discover-explore.md`,
// Task 8) — the integration point where native BiblioCatalog, BiblioDiscoverPage, and
// BiblioExplorePage actually get wired into the app the user sees.
//
// Constructs the REAL BiblioWorld (not a mock) offscreen, with no native BiblioCatalog/Extensions
// context properties present (this is a bare `qml.exe` run — there is no C++ host to register
// them). BiblioWorld's own `typeof BiblioCatalog !== "undefined"` guards handle that null-safely
// by design (mirrors Tankoban/Theatre's identical pattern) — construction and every assertion
// below hold with those globals entirely absent. `Progress`/`Collection` are NOT guarded the same
// way (they're genuine native singletons with no offscreen equivalent); referencing them from
// ContinueRow's item bindings prints a ReferenceError to the console but does not stop
// construction or affect anything this harness asserts — an accepted, pre-existing limitation of
// testing a full WORLD page offscreen (no other *World.qml has a harness at this level either).
//
// House rule: NEVER throw (hangs offscreen) — collect fails, print BIBLIO_WORLD_OK only when
// clean, single Qt.exit(fails.length).
import QtQuick
import "../qml" as UI
import "../qml/BiblioDiscoverApi.js" as Api
import "../qml/BiblioApi.js" as BiblioApi

Item {
    id: harness

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label) }

    // find a descendant by a marker objectName (mirrors comicreader_*_harness.qml's convention)
    function findByObjectName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var f = findByObjectName(kids[i], name)
            if (f) return f
        }
        return null
    }
    function countByObjectName(root, name) {
        if (!root) return 0
        var c = (root.objectName === name) ? 1 : 0
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) c += countByObjectName(kids[i], name)
        return c
    }

    // ── regression guard: BiblioApi.loadBiblio must NEVER be called by BiblioWorld anymore (the
    //    native BiblioCatalog service replaces it as the Featured-carousel source). `.pragma
    //    library` gives ONE shared module instance per engine, so monkey-patching the function
    //    through this harness's own import mutates the SAME object BiblioWorld.qml's import
    //    resolves to. ──
    property bool loadBiblioCalled: false
    property var _realLoadBiblio: BiblioApi.loadBiblio
    Component.onCompleted: {
        BiblioApi.loadBiblio = function(done) {
            harness.loadBiblioCalled = true
            harness._realLoadBiblio(done)
        }
    }

    function fakeExplorePageCatalog() {
        var rows = []
        for (var i = 0; i < 8; i++)
            rows.push({ canonicalId: "b" + i, title: "Book " + i, author: "Auth " + i,
                        coverUrl: "", rating: { average: 4, count: 1 } })
        return {
            ready: true, revision: 1,
            exploreRows: function() { return [
                { catalogId: "popular", items: rows }, { catalogId: "top-rated", items: rows },
                { catalogId: "new-releases", items: rows }, { catalogId: "trending", items: rows }
            ] },
            discoverPage: function() { return { items: rows } },
            mosaic: function() { return rows.slice(0, 5) }
        }
    }
    function fakeExtensionsSource() { return { installed: function() { return [] } } }

    Component { id: worldComp; UI.BiblioWorld {} }
    Component { id: explorePageComp; UI.BiblioExplorePage {} }

    Timer {
        interval: 30
        running: true
        repeat: false
        onTriggered: harness.run()
    }

    function run() {
      try {
        // ═══════════════════════ construction ═══════════════════════
        var world = worldComp.createObject(harness, { width: 1200, height: 900 })
        ok(world !== null, "BiblioWorld constructs offscreen with no native context properties present")
        if (!world) { harness.finish(); return }

        ok(harness.loadBiblioCalled === false,
           "BiblioApi.loadBiblio is never called by BiblioWorld construction (native catalog replaces it)")

        // ═══════════════════════ default tab + tab bar shape ═══════════════════════
        ok(world.activeTab === "discover", "activeTab defaults to discover")

        var tabBar = harness.findByObjectName(world, "biblioTabBar")
        ok(tabBar !== null, "the tab bar is mounted (objectName biblioTabBar)")
        if (tabBar) {
            ok(tabBar.tabModel.length === 2, "the tab bar shows exactly two tabs, got " + tabBar.tabModel.length)
            ok(tabBar.tabModel[0] && tabBar.tabModel[0].key === "discover"
               && tabBar.tabModel[1] && tabBar.tabModel[1].key === "explore",
               "tab order is Discover, Explore, got " + JSON.stringify(tabBar.tabModel))
        }

        // ═══════════════════════ the three shared widgets ═══════════════════════
        // render exactly once, above the tab bar, shared (not duplicated) across both tabs.
        var fc = harness.findByObjectName(world, "biblioFeaturedCarousel")
        var cr1 = harness.findByObjectName(world, "biblioContinueReading")
        var cr2 = harness.findByObjectName(world, "biblioYourCollection")
        ok(fc !== null && cr1 !== null && cr2 !== null,
           "Featured carousel + both Continue rows are all mounted")
        ok(harness.countByObjectName(world, "biblioFeaturedCarousel") === 1, "Featured carousel renders exactly once")
        ok(harness.countByObjectName(world, "biblioContinueReading") === 1, "Continue Reading renders exactly once")
        ok(harness.countByObjectName(world, "biblioYourCollection") === 1, "Your Collection renders exactly once")

        if (fc && tabBar) {
            var board = fc.parent
            ok(board !== null && board === cr1.parent && board === cr2.parent && board === tabBar.parent,
               "the three shared widgets and the tab bar are all siblings on the same board (not per-tab-nested)")
            if (board) {
                var kids = board.children
                var iFc = kids.indexOf(fc), iCr1 = kids.indexOf(cr1), iCr2 = kids.indexOf(cr2), iTab = kids.indexOf(tabBar)
                ok(iFc >= 0 && iCr1 >= 0 && iCr2 >= 0 && iTab >= 0
                   && iFc < iTab && iCr1 < iTab && iCr2 < iTab,
                   "the three shared widgets sit ABOVE the tab bar in board order, got indices "
                   + JSON.stringify({fc:iFc, cr1:iCr1, cr2:iCr2, tab:iTab}))
            }
        }

        // switching tabs never touches the shared widgets (no duplication, no teardown).
        world.activeTab = "explore"
        ok(harness.countByObjectName(world, "biblioFeaturedCarousel") === 1
           && harness.countByObjectName(world, "biblioContinueReading") === 1
           && harness.countByObjectName(world, "biblioYourCollection") === 1,
           "the three shared widgets stay singular after switching to Explore")
        world.activeTab = "discover"

        // ═══════════════════════ Discover renders no rails/mosaics; Explore-only when active ═══════════════════════
        ok(world._discoverPageForTest.visible === true, "Discover page is visible on the default tab")
        ok(world._explorePageForTest.visible === false, "Explore page is hidden on the default tab")
        ok(world._explorePageForTest.height === 0, "Explore page collapses to zero height while hidden (its shelves render nothing)")
        ok(harness.findByObjectName(world._discoverPageForTest, "biblioTabBar") === null,
           "no stray tab bar leaks inside the Discover page's own tree")
        // BiblioDiscoverPage wraps DiscoverBrowser only (Task 5) — it owns no BiblioBookRail of
        // its own, so there is nothing shelf-shaped to leak into Discover regardless of tab state.

        world.activeTab = "explore"
        ok(world._explorePageForTest.visible === true, "Explore page becomes visible once its tab is active")
        ok(world._explorePageForTest.height > 0, "Explore page takes real height once visible (its shelves can render)")
        ok(world._discoverPageForTest.visible === false, "Discover page hides once Explore's tab is active")
        world.activeTab = "discover"

        // ═══════════════════════ pinned navigation + back-restoration ═══════════════════════
        world.activeTab = "explore"
        var explorePageRefBefore = world._explorePageForTest
        var pin = { type: "book", catalogId: "popular", filterGroup: "", filterKey: "", sourceKind: "builtin" }
        world._explorePageForTest.discoverPinRequested(pin)
        ok(world.activeTab === "discover", "BiblioExplorePage.discoverPinRequested(pin) switches activeTab to discover")
        ok(world._discoverPageForTest.pin === pin, "the pin reaches BiblioDiscoverPage.applyPin, got " + JSON.stringify(world._discoverPageForTest.pin))
        ok(world._discoverPageForTest._returnArmed === true,
           "the pin is applied with returnToExplore=true (BiblioDiscoverPage._returnArmed is set)")

        world._discoverPageForTest.exploreReturnRequested()
        ok(world.activeTab === "explore", "BiblioDiscoverPage.exploreReturnRequested() restores the explore tab")
        ok(world._explorePageForTest === explorePageRefBefore,
           "the SAME BiblioExplorePage instance survives the round trip (never destroyed/recreated by a Loader)")
        world.activeTab = "discover"

        // ═══════════════════════ Explore's scroll position survives a visibility round trip ═══════════════════════
        // Empirical proof against the REAL BiblioExplorePage.qml (not invented restore code): a
        // standalone instance, seeded with a real scrolled position at construction (which its
        // own Component.onCompleted mirrors into its internal Flickable), keeps that position
        // through a visible=false -> visible=true cycle with ZERO extra plumbing — because the
        // sibling is never destroyed, only hidden. This is exactly BiblioWorld's own mechanism
        // (`visible: biblio.activeTab === "explore"`), proven directly rather than assumed.
        var scrollPage = explorePageComp.createObject(harness, {
            width: 1200, height: 700,
            catalogSource: harness.fakeExplorePageCatalog(),
            extensionsSource: harness.fakeExtensionsSource(),
            contentY: 180
        })
        ok(scrollPage !== null, "a standalone BiblioExplorePage constructs for the scroll-position proof")
        if (scrollPage) {
            ok(scrollPage.contentY === 180,
               "a construction-time scroll position genuinely reaches the internal Flickable, got " + scrollPage.contentY)
            scrollPage.visible = false
            ok(scrollPage.contentY === 180, "contentY is unchanged immediately after hiding, got " + scrollPage.contentY)
            scrollPage.visible = true
            ok(scrollPage.contentY === 180,
               "contentY is still intact after re-showing — QML preserves a live, non-destroyed sibling's "
               + "scroll position with no restore code needed, got " + scrollPage.contentY)
            scrollPage.destroy()
        }

        // ═══════════════════════ card-open routing: full metadata -> direct bookRequested ═══════════════════════
        var opens = 0, lastBook = null
        world.bookRequested.connect(function(b) { opens++; lastBook = b })

        // Discover tab: a native BiblioWork-shaped card (BiblioDiscoverApi.normalizeBook's own
        // shape) always carries a non-empty `author` — the chosen "has full metadata" heuristic.
        var fullDiscoverCard = Api.normalizeBook({ canonicalId: "w1", title: "Full Book", author: "Real Author", coverUrl: "c.jpg" })
        world._discoverPageForTest.itemOpenRequested(fullDiscoverCard)
        ok(opens === 1 && lastBook === fullDiscoverCard,
           "a full-metadata Discover card routes DIRECTLY through bookRequested (no lookupBook round trip), opens=" + opens)

        // Explore tab: BiblioExplorePage._normalizeHouseItem's shape also always carries `author`.
        var fullExploreCard = { id: "e1", title: "Explore Full", author: "Explore Author", cover: "e.jpg", rating: "4.5", source: "Apple Books · Open Library" }
        world._explorePageForTest.itemRequested(fullExploreCard)
        ok(opens === 2 && lastBook === fullExploreCard,
           "a full-metadata Explore card ALSO routes directly through bookRequested, opens=" + opens)

        // a bare title-only object (no author) never carries enough to open directly — it falls
        // back to the old BiblioApi.lookupBook path, which is async and network-bound, so
        // bookRequested must NOT fire synchronously for it.
        var bareCard = { title: "Bare Title Only" }
        world._discoverPageForTest.itemOpenRequested(bareCard)
        ok(opens === 2, "a bare title-only card does NOT synchronously fire bookRequested (falls to the async lookup path), opens=" + opens)

        // the heuristic itself, directly: presence of a non-empty `author` field.
        ok(world._hasFullMetadata({ title: "T", author: "A" }) === true, "_hasFullMetadata: non-empty author -> true")
        ok(world._hasFullMetadata({ title: "T", author: "" }) === false, "_hasFullMetadata: empty author -> false")
        ok(world._hasFullMetadata({ title: "T" }) === false, "_hasFullMetadata: missing author -> false")
        ok(world._hasFullMetadata("Bare Title String") === false, "_hasFullMetadata: a bare string -> false")

        // ═══════════════════════ Featured carousel: static fallback while no native catalog exists ═══════════════════════
        ok(Array.isArray(world.featuredRows) && world.featuredRows.length > 0,
           "featuredRows is populated (static Catalog.biblioFeatured fallback, no native BiblioCatalog present)")
        ok(world.featuredRows[0] && world.featuredRows[0].raw === undefined,
           "the static fallback slide carries no `raw` native record (routes through openByTitle, not a direct open)")

      } catch (e) {
        harness.fails.push("exception: " + (e && e.message ? e.message : String(e)))
      }
      harness.finish()
    }

    function finish() {
        for (var i = 0; i < harness.fails.length; i++) console.warn("FAIL: " + harness.fails[i])
        if (harness.fails.length === 0) console.warn("BIBLIO_WORLD_OK")
        else console.warn("BIBLIO_WORLD_FAIL: " + harness.fails.length + " failure(s)")
        Qt.exit(harness.fails.length)
    }
}
