// TankobanDiscoverPage — the TANKOBAN wrapper around the shared Discover shell
// (Task 7, arc 2026-08-01).
//
// The browsing surface lives in the world-neutral DiscoverBrowser.qml. This file is the
// thin Tankoban-side wrapper: it builds the Tankoban adapter (TankobanDiscoverApi.js) from
// injected dependencies (MalCatalog / ComicsCatalog context objects, the extension
// registry, the global showExplicitContent preference, and an XMLHttpRequest factory),
// binds it to the shell, and routes a normalized card by type into TankobanWorld's
// existing series doors — Manga cards to seriesRequested(title), Comics cards to
// comicSeriesRequested(item). It owns NO download action and NO series-page UI; the
// existing detail routes are unchanged.
//
// Dependencies are PROPERTIES (not context reads) because the adapter factory needs real
// objects and the page harness builds this bare — a missing MalCatalog/ComicsCatalog is
// null-safe (the adapter returns empty catalogues and the shell shows the empty state).
// showExplicitContent is threaded from WorldPage (Task 7 Step 4) and later from the
// global ContentPreferences (Task 9); the adapter is rebuilt when it changes so the wall
// reflects the live preference without a manual reload.
//
// Public surface for TankobanWorld + the page harness: applyPin(pin),
// mangaSeriesRequested(item), comicSeriesRequested(item), currentType, keyboardMode,
// catalogMenuOpen, catalogMenuModel, items, loading.
import QtQuick
import "TankobanDiscoverApi.js" as Api

Item {
    id: root

    // ── injected dependencies ──
    property var malCatalog: null              // the MalCatalog context object (null-safe)
    property var comicsCatalog: null           // the ComicsCatalog context object (null-safe)
    property var extensions: []                // the extension registry (discoverable catalogues)
    property bool showExplicitContent: false   // global Explicit Content preference (Task 9 threads it live)
    property var pin: null                     // an optional See-all pin (surface compat)

    // ── routing: a normalized card opens the EXISTING series door by type ──
    signal mangaSeriesRequested(var item)      // Manga/Manhwa/Manhua card → seriesRequested(title)
    signal comicSeriesRequested(var item)      // Comics card → comicSeriesRequested(item)

    // the adapter is rebuilt when a dependency that changes its shape changes — extension
    // registry revisions or the explicit preference flip — so the wall stays consistent
    // without a manual reload. adapterRev feeds the shell's adapterRev binding.
    property int _adapterRev: 0
    property var adapter: ({
        types: function() { return [] },
        catalogs: function() { return [] },
        defaultCatalog: function() { return "" },
        filters: function() { return [] },
        resolvePin: function() { return { missing: true } },
        fetchPage: function(s, c, g, done) { done(g, { items: [], nextCursor: null, exhausted: true, freshness: "bundled", warning: "" }) }
    })

    function _rebuildAdapter() {
        // XMLHttpRequest factory: the engine supplies the real one in-app; the harness
        // injects a fake via a property so the Jikan refresh path is observable offline.
        var xhrFactory = (typeof XMLHttpRequest !== "undefined")
                       ? function() { return new XMLHttpRequest() } : null
        adapter = Api.create(malCatalog, comicsCatalog, extensions,
                             showExplicitContent, _xhrFactory || xhrFactory)
        _adapterRev++
        if (browser.adapter) browser.refresh()
    }

    property var _xhrFactory: null             // harness override; null in-app

    Component.onCompleted: _rebuildAdapter()

    // Rebuild when a shape-affecting dependency changes. Catalog object identity is stable
    // for the app lifetime (context properties), so this fires on extension registry
    // revisions and on an explicit-preference flip — exactly the two shape changes.
    onExtensionsChanged: _rebuildAdapter()
    onShowExplicitContentChanged: _rebuildAdapter()

    // Data-vault Slice 3 (2026-08-22): the wall is empty-because-downloading only when its
    // CURRENT type's own catalog is the one still landing. malCatalog/comicsCatalog are
    // typeof-guarded (both null-safe by contract, per the file header) and each carries a
    // Q_INVOKABLE ready() (not a plain-bindable property — see Main.qml's catalogVaultState
    // comment), so this reads it directly rather than through a Connections rev counter: the
    // wake-on-ready Connections below already forces a reload (and therefore a re-render) the
    // moment readiness flips, which is the only time this expression's answer can change.
    readonly property bool _catalogueDownloading:
        (typeof CatalogVault !== "undefined" && CatalogVault && CatalogVault.fetching) &&
        ((browser.currentType === "manga" && malCatalog && !malCatalog.ready()) ||
         (browser.currentType === "comics" && comicsCatalog && !comicsCatalog.ready()))

    // Wake-on-ready: a fresh install boots with an empty wall while the vault download is
    // still in flight. The moment the backing catalog flips ready, re-run the initial page
    // fetch so the wall paints without the user having to switch types or reopen the page.
    // Guarded on items.length === 0 so a wall that is already populated (dev machine, or a
    // catalog that flips ready a second time for an unrelated reason) never reloads under
    // the user's hand.
    Connections {
        target: (typeof MalCatalog !== "undefined") ? MalCatalog : null
        function onReadyChanged() {
            if (browser.currentType === "manga" && browser.items.length === 0) browser.reloadCurrent()
        }
    }
    Connections {
        target: (typeof ComicsCatalog !== "undefined") ? ComicsCatalog : null
        function onReadyChanged() {
            if (browser.currentType === "comics" && browser.items.length === 0) browser.reloadCurrent()
        }
    }

    DiscoverBrowser {
        id: browser
        anchors.fill: parent
        adapter: root.adapter
        fallbackType: "manga"                  // Tankoban's default type when catalog objects are absent
        // Gallery poster profile for BOTH Manga and Comics (one shared shell) — adopted after the
        // Theatre pilot + a manga/comics cover screenshot check. Presentation only; demographic and
        // publisher stay in the Discover filters, never on the card. The 2:3 crop is identical to the
        // classic card (both use ratio 1.5), so gallery introduces no NEW cover cropping.
        posterVisualProfile: "gallery"
        catalogueDownloading: root._catalogueDownloading
        // Tankoban copy — the wall's empty-state wording.
        textNoCatalogue: "Nothing to browse here yet."
        textCatalogueEmpty: "This catalogue answered with nothing."
        textFilterEmpty: "No series match this filter."
        noticeMissingFormat: "That source is no longer available — showing the built-in catalogue instead."
        // route the shell's normalized card by type to the existing series doors.
        onItemOpenRequested: function(item) {
            if (!item) return
            if (item.type === "comics") root.comicSeriesRequested(item)
            else root.mangaSeriesRequested(item)
        }
    }

    // ── public aliases kept for TankobanWorld + the page harness ──
    property alias currentType: browser.currentType
    property alias keyboardMode: browser.keyboardMode
    property alias catalogMenuOpen: browser.catalogMenuOpen
    readonly property alias catalogMenuModel: browser.catalogMenuModel
    property alias items: browser.items
    property alias loading: browser.loading

    function applyPin(p) {
        pin = p || null
        browser.applyPin(p)
    }

    // Test seam: a named reference to the inner shell so the page harness can drive the
    // real card-routing path (browser.itemOpenRequested) without poking children[].
    // Production code never reads this; it exists for tests only.
    readonly property Item _shellForTest: browser
}
