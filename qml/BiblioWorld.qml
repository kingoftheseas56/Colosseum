// BiblioWorld - the Colosseum world page for books. Owner: A2.
// Same spine as Tankoban/Theatre: Featured carousel + Continue rows stay shared ABOVE a
// Discover | Explore tab split (plan `2026-08-01-biblio-discover-explore.md`, Task 8) — the
// integration point where the native BiblioCatalog service (Task 4), BiblioDiscoverPage (Task 5),
// and BiblioExplorePage (Task 7) actually get wired into the app.
//
// Discover is the utilitarian catalogue grid (BiblioDiscoverPage -> the shared DiscoverBrowser
// shell) with no shelves of its own. Explore is the deep shelf page (Top 10 + house rails +
// extension previews + the three fixed mosaics). BOTH tabs are declared directly (mirrors
// TankobanDiscoverPage / TheatreCatalogPage) rather than Loader-swapped, so a tab switch never
// destroys either page — Explore's Flickable keeps its own scroll position alive the whole time,
// hidden or not, with zero extra restore plumbing (verified in tests/biblio_world_harness.qml).
//
// GenreMosaic/BiblioGenrePage/BiblioGenreIndex are RETIRED from this world (their native-backed
// replacement is Explore's three fixed mosaics) but left in place elsewhere for compatibility
// until a later cleanup — this file just stops reaching them.
//
// Delivery (search + download) stays libgen from TB2 - a separate layer, like Cinemeta vs the
// Theatre addon. BiblioApi.search/lookupBook/searchAudiobooks/pairing helpers are untouched.

import QtQuick
import "Catalog.js" as Catalog
import "BiblioApi.js" as BiblioApi
import "VaultApi.js" as VaultApi

WorldPage {
    id: biblio
    medium: "Biblio"

    // Discover is always the entry tab — never restored from settings, every fresh load of
    // Biblio starts here (plan interface: "no persistence of which tab was last open").
    property string activeTab: "discover"
    // The global Explicit Content preference (Main.qml binds it, mirrors showExplicitContent's
    // existing guarded-binding pattern one property over — see Main.qml's world-loader onLoaded).
    property bool showExplicit: false

    property var featuredRows: Catalog.biblioFeatured
    signal collectionOpenRequested(var entry)

    // ── Featured carousel hydration: native Popular once BiblioCatalog is ready, the static
    //    Catalog.biblioFeatured fallback until then. loadBiblio() is NOT called here anymore —
    //    the native service is the source of truth once it has a published snapshot. ──
    function _slideFromNative(row, index) {
        var t = BiblioApi.tone(index)
        var r = row || {}
        return {
            title: r.title || "",
            blurb: r.author ? ("By " + r.author + ".") : "Popular on Biblio.",
            art: r.coverUrl || "",
            artKind: "poster",
            ghost: "B",
            c1: t[0], c2: t[1],
            raw: r                 // full native BiblioWork row — feeds a direct bookRequested open
        }
    }
    function _refreshFeaturedFromNative() {
        if (!biblio.lifecycleActive)
            return
        if (typeof BiblioCatalog === "undefined" || !BiblioCatalog || BiblioCatalog.ready !== true)
            return
        var page = BiblioCatalog.discoverPage("popular", "", "", biblio.showExplicit, 0, 4)
        var rows = (page && page.items) ? page.items : []
        if (rows.length > 0)
            biblio.featuredRows = rows.map(biblio._slideFromNative)
    }
    Component.onCompleted: biblio._refreshFeaturedFromNative()
    onLifecycleActiveChanged: if (biblio.lifecycleActive) biblio._refreshFeaturedFromNative()
    onShowExplicitChanged: biblio._refreshFeaturedFromNative()
    // BiblioCatalog is a native context property (identity stable for the app lifetime, per
    // BiblioDiscoverPage's own doc comment) — reacting to its ready/revision signals is how a
    // cold-boot page catches the first successful daily refresh without a manual reload.
    Connections {
        target: (typeof BiblioCatalog !== "undefined") ? BiblioCatalog : null
        function onReadyChanged() { biblio._refreshFeaturedFromNative() }
        function onRevisionChanged() { biblio._refreshFeaturedFromNative() }
    }

    // ── card-open routing: a native BiblioWork-shaped card (Discover/Explore) carries real
    //    title/author/cover/rating fields already — open it directly via the inherited
    //    bookRequested door, no round trip. A bare title-only object (the legacy Featured
    //    fallback shape) still needs the old iTunes lookup to become a full book. Heuristic: a
    //    non-empty `author` field marks a card as already-full metadata — every native card
    //    (BiblioDiscoverApi.normalizeBook / BiblioExplorePage._normalizeHouseItem /
    //    _normalizeExtItem) always carries this field; a bare title string or a static
    //    Catalog.biblioFeatured slide never does. ──
    function _hasFullMetadata(item) {
        return !!(item && typeof item === "object" && item.author && String(item.author).length > 0)
    }
    function openBookCard(item) {
        if (biblio._hasFullMetadata(item)) { biblio.bookRequested(item); return }
        var title = (item && typeof item === "object") ? item.title : item
        biblio.openByTitle(title)
    }
    // tap a bare title → fetch its full detail by title, then open the dust-jacket page
    function openByTitle(title) {
        if (!title) return
        BiblioApi.lookupBook(title, function(b) { if (b) biblio.bookRequested(b) })
    }
    function _openFeaturedSlide(i) {
        var s = biblio.featuredRows[i]
        if (!s) return
        if (s.raw) biblio.bookRequested(s.raw)          // native slide → direct open, full metadata
        else biblio.openByTitle(s.title || "")           // static fallback slide → old lookup path
    }

    // ── the three shared widgets: rendered ONCE, above the tab bar, identical across both tabs ──
    FeaturedCarousel {
        objectName: "biblioFeaturedCarousel"
        kicker: "Featured in Biblio"
        primaryLabel: "Read"
        secondaryLabel: "Details"
        slides: biblio.featuredRows
        onPrimaryClicked: (i) => biblio._openFeaturedSlide(i)
        onSecondaryClicked: (i) => biblio._openFeaturedSlide(i)
    }

    ContinueRow {
        objectName: "biblioContinueReading"
        title: "Continue Reading"
        // Do not hydrate the Progress-backed row while this retained world is hidden. The live
        // binding re-evaluates on activation and keeps the existing Continue signal wiring.
        items: biblio.lifecycleActive
               ? (Progress.revision, VaultApi.recentWithoutVault(Progress, "book", 12)) : []
        onResumeRequested: (item) => biblio.continueResumeRequested(item)
        onDetailRequested: (item) => biblio.continueDetailRequested(item)
        onSeeAllRequested: biblio.continueSeeAllRequested()
    }

    // Your Collection row REMOVED (Hemanth 2026-08-07): it was functionally identical to the
    // Library tab — same Collection.items("biblio") source, two surfaces. Library is the single
    // home for saved books now; Continue Reading (above) carries the in-progress titles.

    // Discover | Explore | Library — Discover first and default (mirrors Theatre/Tankoban's
    // discover-first tab order). Library is the Theatre-parity retained saved-shelf page (plan
    // 2026-08-06-biblio-library-tab-theatre-parity.md, Slice 2): one Collection entry → one card.
    WorldTabBar {
        objectName: "biblioTabBar"
        // Biblio-scoped pill stem: worldTab_<key> collides with Tankoban's Library/Manga/Comics
        // pills when the warmer pre-builds Tankoban's world (resolveTarget's DFS walks hidden
        // worlds too) — biblioTab_<key> is unique to this bar. See WorldTabBar.tabPrefix.
        tabPrefix: "biblioTab"
        backdrop: biblio.backdrop
        currentTab: biblio.activeTab
        tabModel: [ { key: "discover", label: "Discover" },
                    { key: "explore", label: "Explore" },
                    { key: "library", label: "Library" } ]
        onTabRequested: (tab) => biblio.activeTab = tab
    }

    // ── Discover: the shared Discover shell (utilitarian grid, no shelves of its own). Retained
    //    (not Loader-swapped) so its in-session catalogue/filter/scroll state survives tab
    //    switches, exactly like Theatre's DiscoverPage. ──
    BiblioDiscoverPage {
        id: discoverPage
        // Automation identity (Lanista): the pilot scenario waits on this page's `loading`
        // flag instead of sleeping — a name is what makes that wait addressable.
        objectName: "biblioDiscoverPage"
        visible: biblio.activeTab === "discover"
        width: parent.width
        height: visible ? Math.max(620, biblio.height - 200) : 0
        active: biblio.lifecycleActive && visible
        biblioCatalog: (typeof BiblioCatalog !== "undefined") ? BiblioCatalog : null
        extensions: (typeof Extensions !== "undefined") ? Extensions.installed() : []
        showExplicit: biblio.showExplicit
        onItemOpenRequested: (item) => biblio.openBookCard(item)
        // Explore sent us here with a return affordance armed (applyPin's returnToExplore) —
        // Back hands control straight back to Explore, whose own Flickable never moved.
        onExploreReturnRequested: biblio.activeTab = "explore"
    }

    // ── Explore: the deep shelf page (Top 10 + house rails + extension previews + the three
    //    fixed mosaics). catalogSource/extensionsSource auto-default to the global BiblioCatalog/
    //    Extensions context properties (see BiblioExplorePage.qml) — no need to bind them here.
    //    Deviation from the plan's literal wording: the plan says to mirror TheatreCatalogPage's
    //    bare `implicitHeight` (its root is a Column, which self-sizes from its children). Read
    //    against the actual file, BiblioExplorePage's root is a plain Item wrapping its OWN
    //    internal Flickable (own ScrollGlide + HouseScrollBar, exactly like DiscoverBrowser) —
    //    `implicitHeight` on an Item is NOT auto-derived from an anchors.fill child, so that
    //    expression would collapse Explore to 0 height. It is architecturally a bounded,
    //    self-scrolling viewport, not a self-sizing block — so it gets the SAME viewport-height
    //    expression as Discover, not Theatre's Column-only shorthand. ──
    BiblioExplorePage {
        id: explorePage
        // Automation identity (Lanista): the catalog-source smoke waits on this page's
        // housesLoading flag and reads displayRows/houseRowsMap by name — the same
        // addressability contract biblioDiscoverPage set (2026-08-15 OL-catalog arc).
        objectName: "biblioExplorePage"
        visible: biblio.activeTab === "explore"
        width: parent.width
        height: visible ? Math.max(620, biblio.height - 200) : 0
        active: biblio.lifecycleActive && visible
        showExplicit: biblio.showExplicit
        onItemRequested: (item) => biblio.openBookCard(item)
        onDiscoverPinRequested: (pin) => {
            biblio.activeTab = "discover"
            discoverPage.applyPin(pin, true)
        }
    }

    // ── Library: the Theatre-parity retained saved-shelf page (Slice 2). One Collection entry
    //    → one card; conservative Progress match enables Resume, else the card opens Details.
    //    Retained (not Loader-swapped) so its search/filter/sort/scroll state survives tab
    //    switches, exactly like Discover/Explore. Routing reuses the existing Main.qml doors:
    //      resumeRequested  → continueResumeRequested  (Main.resumeContinue, biblio branch)
    //      detailRequested  → collectionOpenRequested   (Main.openCollectionEntry, requires e.world)
    //      removeRequested  → INLINE Collection.remove("biblio", id) (membership only — mirrors
    //                         Theatre/Tankoban's inline remove; never Progress, never files). ──
    BiblioLibraryPage {
        id: libraryPage
        objectName: "biblioLibraryPage"
        visible: biblio.activeTab === "library"
        width: parent.width
        height: visible ? Math.max(620, biblio.height - 200) : 0
        onResumeRequested: (record) => biblio.continueResumeRequested(record)
        onDetailRequested: (entry) => biblio.collectionOpenRequested(entry)
        onRemoveRequested: (entry) => {
            if (typeof Collection !== "undefined") Collection.remove("biblio", String(entry.id))
        }
    }

    // Test seams: named references so the world harness can drive the real tab/pin/card-open
    // wiring without poking children[]. Production code never reads these; tests only (mirrors
    // BiblioDiscoverPage's own `_shellForTest` convention).
    readonly property Item _discoverPageForTest: discoverPage
    readonly property Item _explorePageForTest: explorePage
    readonly property Item _libraryPageForTest: libraryPage
}
