// BiblioDiscoverPage — the BIBLIO wrapper around the shared Discover shell (Task 5, arc 2026-08-01).
//
// The browsing surface lives in the world-neutral DiscoverBrowser.qml. This file is the thin
// Biblio-side wrapper: it builds the Biblio adapter (BiblioDiscoverApi.js) from injected
// dependencies (the native BiblioCatalog context object, the extension registry, and the global
// Explicit Content preference), binds it to the shell, and re-emits a normalized card as
// itemOpenRequested for the book detail route. It owns NO acquisition and NO detail-page UI.
//
// Dependencies are PROPERTIES (not context reads) because the adapter factory needs a real
// BiblioCatalog-shaped object and the page harness builds this bare — a missing biblioCatalog is
// null-safe (the adapter returns an empty built-in wall and the shell shows the empty state).
// showExplicit is INJECTED, never read from Main's private contentPreferences id — whoever loads
// this page (a later task) binds it in from the outside, same as Tankoban's page does for
// showExplicitContent.
//
// applyPin(pin, returnToExplore) opens a built-in/extension catalogue exactly like every other
// world's applyPin, PLUS remembers whether the caller wants an Explore-return affordance. When
// armed, the shell's generic backRequested() (a plain "user pressed back" signal the shell itself
// never acts on) is re-emitted here as the Biblio-specific exploreReturnRequested() — the shell
// has no idea what "Explore" is; this wrapper is the one that does.
//
// Public surface for BiblioWorld + the page harness: applyPin(pin, returnToExplore),
// itemOpenRequested(item), exploreReturnRequested(), currentType, keyboardMode, catalogMenuOpen,
// catalogMenuModel, items, loading.
import QtQuick
import "BiblioDiscoverApi.js" as Api

Item {
    id: root

    // ── injected dependencies ──
    property var biblioCatalog: null           // the native BiblioCatalog context object (null-safe)
    property var extensions: []                // the extension registry (discoverable book catalogues)
    property bool showExplicit: false          // global Explicit Content preference, injected live
    property var pin: null                     // an optional See-all pin (surface compat)

    // ── routing: a normalized card opens the EXISTING book detail door ──
    signal itemOpenRequested(var item)
    // Explore-return: fires when the shell's generic backRequested() arrives while a pin's
    // returnToExplore was armed. The shell never knows about "Explore" — only this wrapper does.
    signal exploreReturnRequested()

    property bool _returnArmed: false

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
        adapter = Api.create(biblioCatalog, extensions, showExplicit)
        _adapterRev++
        if (browser.adapter) browser.refresh()
    }

    Component.onCompleted: _rebuildAdapter()

    // Rebuild when a shape-affecting dependency changes. biblioCatalog's identity is stable for
    // the app lifetime (a context property), so this fires on extension registry revisions and
    // on an explicit-preference flip — exactly the two shape changes.
    onExtensionsChanged: _rebuildAdapter()
    // An explicit-preference flip needs MORE than refresh() gives: refresh() only re-fetches when
    // the current catalogue disappeared from the adapter's list, which never happens here (Popular
    // etc. never disappear) — so the wall's displayed `items` would never actually change even
    // though the adapter object itself was rebuilt with the new showExplicit baked in. Force a
    // genuine reload of the SAME catalogue/filter selection so a stale explicit (or stale hidden)
    // item never persists on screen after a live flip.
    onShowExplicitChanged: {
        _rebuildAdapter()
        if (browser.adapter) browser.reloadCurrent()
    }

    DiscoverBrowser {
        id: browser
        anchors.fill: parent
        adapter: root.adapter
        fallbackType: "book"                   // Biblio's only type when the catalog object is absent
        // Gallery poster profile — same shared card polish Tankoban/Theatre already ship.
        posterVisualProfile: "gallery"
        // Biblio's Discover-specific card hooks: author always visible, rating/source revealed
        // on hover OR keyboard focus (spec: "hidden at rest, revealed on pointer hover or
        // keyboard focus").
        showAuthorAtRest: true
        showSourceOnReveal: true
        // Pin cards to the approved 148px gallery token instead of stretching to fill residual
        // column width — book covers are small-source art, so overstretching them is what turns
        // an ordinary cover blurry (2026-08-06 shelf-quality pass).
        fixedGalleryWidth: true
        // the back affordance shows only when this page actually has somewhere to return to.
        showBackAction: root._returnArmed
        // Biblio copy — the wall's empty-state wording.
        textNoCatalogue: "Nothing to browse here yet."
        textCatalogueEmpty: "This catalogue answered with nothing."
        textFilterEmpty: "No books match this filter."
        noticeMissingFormat: "That source is no longer available — showing the built-in catalogue instead."
        onItemOpenRequested: (item) => root.itemOpenRequested(item)
        onBackRequested: {
            if (root._returnArmed) {
                root._returnArmed = false
                root.exploreReturnRequested()
            }
        }
    }

    // ── public aliases kept for BiblioWorld + the page harness ──
    property alias currentType: browser.currentType
    property alias keyboardMode: browser.keyboardMode
    property alias catalogMenuOpen: browser.catalogMenuOpen
    readonly property alias catalogMenuModel: browser.catalogMenuModel
    property alias items: browser.items
    property alias loading: browser.loading

    function applyPin(p, returnToExplore) {
        pin = p || null
        root._returnArmed = returnToExplore === true
        browser.applyPin(p)
    }

    // Test seam: a named reference to the inner shell so the page harness can drive the
    // real card-routing/back path without poking children[]. Production code never reads
    // this; it exists for tests only.
    readonly property Item _shellForTest: browser
}
