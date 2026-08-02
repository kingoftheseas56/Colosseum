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

    DiscoverBrowser {
        id: browser
        anchors.fill: parent
        adapter: root.adapter
        fallbackType: "manga"                  // Tankoban's default type when catalog objects are absent
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
