// DiscoverPage — the THEATRE wrapper around the shared Discover shell (Task 3, 2026-08-01).
//
// Discover's browsing surface now lives in the world-neutral DiscoverBrowser.qml. This file is
// the thin Theatre-side adapter: it reads the `Extensions` context property live, translates
// DiscoverApi.js's output into the shell's shared contract, and hands the shell that adapter.
// Everything Theatre users saw — the masthead, the genre filter, the poster wall, skip-paging,
// keyboard focus — is the shell's copied visual tree. The transport underneath (Cinemeta
// fallback, extension ordering, URLs, required extras, skip paging) is UNTOUCHED: the adapter
// only re-shapes what DiscoverApi already returns, and its fetchPage rebuilds the FULL selections
// map (required-extra defaults included) before every loadPage.
//
// Public surface kept for its consumers (TheatreWorld + the page harness): applyPin(pin),
// itemOpenRequested(item), currentType, keyboardMode, catalogMenuOpen, catalogMenuModel.
// itemOpenRequested re-emits the RAW meta so the detail page receives the exact object it
// always did.
import QtQuick
import "DiscoverApi.js" as Api

Item {
    id: disco

    signal itemOpenRequested(var item)      // a click / Enter on a poster opens the detail page
    property var pin: null                   // an optional See-all pin (surface compat)

    // ── the Theatre adapter: DiscoverApi.js -> the shell's shared contract ──
    // Construction MUST stay safe with NO Extensions context property (the page harness
    // builds this bare) — every read is guarded by the typeof check.
    QtObject {
        id: theatreAdapter
        property var installed: (typeof Extensions !== "undefined") ? Extensions.installed() : []

        function refresh() {
            installed = (typeof Extensions !== "undefined") ? Extensions.installed() : []
        }

        function types() { return Api.shellTypes(installed) }
        function catalogs(type) { return Api.shellCatalogs(installed, type) }
        function defaultCatalog(type) { return Api.shellDefaultCatalog(installed, type) }
        function filters(type, catalogKey) {
            var cat = Api.catalogByKey(installed, type, catalogKey)
            return cat ? Api.shellFilters(cat) : []
        }
        function resolvePin(pin) { return Api.shellResolvePin(installed, pin) }

        function fetchPage(state, cursor, generation, done) {
            var cat = Api.catalogByKey(installed, state.type, state.catalogKey)
            if (!cat) { done(generation, { items: [], nextCursor: null, exhausted: true, freshness: "", warning: "" }); return }
            // rebuild the FULL selections map (required extras kept) then override with the
            // shell's single active filter — this is the Theatre-preserving translation.
            var selections = Api.selectionsForFilter(cat, state.filterGroup, state.filterKey)
            var skip = cursor || 0
            Api.loadPage(cat, selections, skip, function(metas) {
                var items = []
                for (var i = 0; i < metas.length; i++) items.push(Api.normalizeMeta(metas[i], cat.type))
                var next = metas.length ? (skip + metas.length) : null
                done(generation, { items: items, nextCursor: next,
                                   exhausted: metas.length === 0, freshness: "", warning: "" })
            })
        }
    }

    DiscoverBrowser {
        id: browser
        anchors.fill: parent
        adapter: theatreAdapter
        fallbackType: "movie"                // Theatre's default type when the registry is bare
        // Theatre copy — preserves the old empty-state wording (no filtered variant existed).
        textNoCatalogue: "No catalogues here — install an addon that carries some."
        textCatalogueEmpty: "This catalogue answered with nothing."
        textFilterEmpty: "This catalogue answered with nothing."
        noticeMissingFormat: "This catalogue needs the %1 addon."
        // the shell emits the normalized card; Theatre downstream expects the RAW meta.
        onItemOpenRequested: (item) => disco.itemOpenRequested((item && item.raw !== undefined) ? item.raw : item)
    }

    // ── public aliases kept for TheatreWorld + the regression harnesses ──
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

    // a live registry change re-derives the adapter, then refreshes the shell.
    Connections {
        target: (typeof Extensions !== "undefined") ? Extensions : null
        function onChanged() { theatreAdapter.refresh(); browser.refresh() }
    }
}
