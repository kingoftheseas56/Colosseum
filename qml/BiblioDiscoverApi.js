// BiblioDiscoverApi.js — the Biblio adapter for the shared Discover shell (Task 5, arc 2026-08-01).
//
// The shell (DiscoverBrowser.qml) is WORLD-NEUTRAL: it speaks a small adapter contract
// (types/catalogs/filters/defaultCatalog/resolvePin/fetchPage) and renders normalized cards.
// This file IS the Biblio adapter — it owns the four house catalogue descriptors (Popular / Top
// Rated / New Releases / Trending, backed by the native BiblioCatalog service), the controlled
// filter shape (BiblioCatalog.filterGroups), normalization of native rows into the shell's card
// shape (with `author`/`source` for Biblio's author-at-rest and source-on-reveal hooks), the
// "From Your Extensions" book-catalogue seam riding the SAME Stremio-style extension transport
// Theatre's DiscoverApi.js already exposes, and See-all pin resolution. It performs NO acquisition
// and owns NO detail-page UI — the page wrapper routes a normalized card to the existing book
// detail route.
//
// A .pragma library CANNOT see QML context properties, so dependencies are passed explicitly
// into create(): the BiblioCatalog context object (native, or a test fake with the same
// invokable names), the extension registry, and the global showExplicit flag. Unlike Tankoban's
// manga path, built-in fetches are SYNCHRONOUS native invokables (no XHR/callback indirection).
.pragma library
.import "DiscoverApi.js" as DiscoverApi
.import "AddonClient.js" as AddonClient
.import "ExplicitContentPolicy.js" as Policy

// Exposed for a static-contract grep (mirrors TankobanDiscoverApi.SOURCE).
var SOURCE = "biblio-discover-adapter";

// --- catalogue descriptors ---
// The four house catalogues BiblioCatalog computes, in native's fixed order (spec 2026-08-01).
var BOOK_CATALOGS = [
    { key: "popular",      title: "Popular",      sourceKind: "builtin" },
    { key: "top-rated",    title: "Top Rated",    sourceKind: "builtin" },
    { key: "new-releases", title: "New Releases", sourceKind: "builtin" },
    { key: "trending",     title: "Trending",     sourceKind: "builtin" }
];
var BUILTIN_SECTION = "Biblio";
var BUILTIN_ATTRIBUTION = "Biblio built-in catalogue";
var EXTENSIONS_SECTION = "From Your Extensions";
// the shell's default offlineWarning text (DiscoverBrowser.qml) — matched EXACTLY so a stale
// snapshot's offline notice actually trips the shell's showOfflineNotice banner.
var OFFLINE_WARNING = "Showing offline catalogue";

function stableKey(s) { return String(s || "").toLowerCase().trim() }

// --- extension catalogue seam ---
// Biblio's "From Your Extensions" rides the SAME Stremio-style extension registry/transport
// Theatre already uses (DiscoverApi.js / AddonClient.js) — not Tankoban's manifest.catalogs/
// isDiscoverable convention. Technical call: gate on AddonClient.discoverBrowsable() (the exact
// required-extras check DiscoverApi.js already applies) plus two Biblio-specific exclusions:
// a core row (the native Apple/Open Library source itself, never a user-facing "extension
// catalogue" choice) and any catalog an addon explicitly marks acquisition/download-only via
// isDiscoverable === false. Key format matches DiscoverApi.catalogsFor's exactly (transportUrl|
// type|catalogId) so DiscoverApi.catalogByKey/selectionsForFilter/loadPage resolve it untouched.
function isAppleBooksSource(id, name) {
    var s = (String(id || "") + " " + String(name || "")).toLowerCase();
    return s.indexOf("apple") >= 0 && s.indexOf("book") >= 0;
}

function extensionCatalogs(extensions) {
    var out = [];
    for (var i = 0; i < (extensions || []).length; i++) {
        var e = extensions[i];
        if (!e || e.enabled !== true) continue;
        if (e.core === true) continue;                          // core rows are the native Apple/OL source
        var m = e.manifest || {};
        if (isAppleBooksSource(m.id || e.id, m.name)) continue;  // Apple Books is never a browsable extension choice
        var cats = m.catalogs || [];
        for (var j = 0; j < cats.length; j++) {
            var c = cats[j];
            if (!c || !c.id || c.type !== "book") continue;
            if (c.isDiscoverable === false) continue;             // an addon may mark a catalog acquisition/download-only
            if (!AddonClient.discoverBrowsable(c)) continue;       // required extras it can't auto-answer are excluded
            out.push({
                key: String(e.transportUrl) + "|book|" + String(c.id),
                title: c.name || m.name || "Catalog",
                sourceKind: "extension",
                section: EXTENSIONS_SECTION,
                attribution: m.name || e.id || "Extension"
            });
        }
    }
    return out;
}

function catalogsForType(extensions) {
    var out = [];
    for (var i = 0; i < BOOK_CATALOGS.length; i++) {
        out.push({
            key: BOOK_CATALOGS[i].key,
            title: BOOK_CATALOGS[i].title,
            sourceKind: BOOK_CATALOGS[i].sourceKind,
            section: BUILTIN_SECTION,
            attribution: BUILTIN_ATTRIBUTION
        });
    }
    var exts = extensionCatalogs(extensions || []);
    for (var k = 0; k < exts.length; k++) out.push(exts[k]);
    return out;
}

// --- filter shape ---
// BiblioCatalog.filterGroups(includeExplicit) -> [{axis, label, facets:[{key,label}]}] (the
// native/store shape). Re-shape into the shell's [{group, options:[{key,label}]}] contract,
// keeping `axis` alongside for pin canonicalization below (the shell ignores unknown fields).
function buildFilterOptions(facets) {
    var out = [];
    for (var i = 0; i < (facets || []).length; i++) {
        var f = facets[i];
        out.push({ key: stableKey(f.key), label: f.label || f.key || "" });
    }
    return out;
}

function filterGroupsFor(biblioCatalog, showExplicit) {
    if (!biblioCatalog) return [];
    var groups = biblioCatalog.filterGroups(showExplicit) || [];
    var out = [];
    for (var i = 0; i < groups.length; i++) {
        var g = groups[i];
        var opts = buildFilterOptions(g.facets);
        if (opts.length) out.push({ group: g.label || g.axis, axis: g.axis, options: opts });
    }
    return out;
}

// --- normalization ---
// A native BiblioCatalog::discoverPage row -> the shell's normalized card. `author` feeds
// showAuthorAtRest; `source` is left empty for built-ins (native does not surface per-item
// provenance through discoverPage — never invent an attribution it didn't report).
function normalizeBook(row) {
    var r = row || {};
    var year = 0;
    var d = String(r.canonicalFirstPublished || "");
    var ym = /^(\d{4})/.exec(d);
    if (ym) year = parseInt(ym[1], 10);
    var ratingAvg = (r.rating && r.rating.average !== undefined && r.rating.average !== null)
                   ? r.rating.average : 0;
    return {
        id: r.canonicalId || "",
        type: "book",
        title: r.title || "",
        cover: r.coverUrl || "",
        year: year,
        rating: ratingAvg,
        format: "",
        publisher: r.publisher || "",
        author: r.author || "",
        source: "",
        availability: true,           // acquisition/ownership never affects discovery or ranking
        explicit: false,
        raw: r
    };
}

// An extension book-catalogue meta -> the shell's normalized card. `source` carries the
// owning extension's attribution (the one honest per-item provenance we DO have here).
function normalizeExtensionBook(meta, catalog) {
    var m = meta || {};
    return {
        id: (m.id !== undefined && m.id !== null) ? m.id : "",
        type: "book",
        title: m.title || m.name || m.caption || "",
        cover: m.cover || m.poster || "",
        year: m.releaseInfo || m.year || "",
        rating: m.imdbRating || m.rating || "",
        format: "",
        publisher: m.publisher || "",
        author: m.author || "",
        source: (catalog && catalog.addonName) || "",
        availability: true,
        explicit: false,
        raw: m
    };
}

// --- pin resolution ---
// canonicalFilterGroup: accepts either the controlled axis ("genre") or the display label
// ("Genre") and resolves it to the live group's display label; unknown groups return "".
function canonicalFilterGroup(group, groups) {
    var g = stableKey(group);
    if (!g.length) return "";
    for (var i = 0; i < groups.length; i++) {
        var grp = groups[i];
        if (stableKey(grp.axis) === g || stableKey(grp.group) === g) return grp.group;
    }
    return "";
}

// validateFilterKey: checks the pin's filter key against the LIVE facets for the canonicalized
// group so a stale key (a facet that no longer exists) is dropped.
function validateFilterKey(filterGroup, filterKey, groups) {
    if (!filterKey || !filterKey.length) return false;
    var fk = stableKey(filterKey);
    for (var i = 0; i < groups.length; i++) {
        if (groups[i].group !== filterGroup) continue;
        var opts = groups[i].options || [];
        for (var j = 0; j < opts.length; j++)
            if (stableKey(opts[j].key) === fk) return true;
    }
    return false;
}

function resolvePin(pin, deps) {
    var p = pin || {};
    // Biblio surfaces exactly one type ("book"); a pin naming any other type is missing.
    if (p.type && p.type !== "book") {
        return { missing: true, type: p.type, catalogKey: "", filterGroup: "", filterKey: "",
                 missingName: "" };
    }
    var cats = catalogsForType(deps.extensions);
    var catalogKey = "";
    for (var i = 0; i < cats.length; i++)
        if (cats[i].key === p.catalogId) { catalogKey = p.catalogId; break; }
    // a stale/removed catalogue (most commonly a since-uninstalled extension) falls back to
    // the Popular built-in — the pin's type is still Biblio, so this is NOT "missing".
    if (!catalogKey.length) catalogKey = "popular";
    var groups = filterGroupsFor(deps.biblioCatalog, deps.showExplicit);
    var canonical = canonicalFilterGroup(p.filterGroup, groups);
    var keepFilter = canonical.length > 0 && validateFilterKey(canonical, p.filterKey, groups);
    return {
        missing: false,
        type: "book",
        catalogKey: catalogKey,
        filterGroup: keepFilter ? canonical : "",
        filterKey: keepFilter ? stableKey(p.filterKey) : "",
        missingName: ""
    };
}

// --- page delivery ---
function fetchBuiltinPage(deps, state, cursor, generation, done) {
    var bc = deps.biblioCatalog;
    var offset = (cursor !== null && cursor !== undefined) ? cursor : 0;
    var limit = 24;
    if (!bc) {
        done(generation, { items: [], nextCursor: null, exhausted: true, freshness: "bundled", warning: "" });
        return;
    }
    var groups = filterGroupsFor(bc, deps.showExplicit);
    var canonical = canonicalFilterGroup(state.filterGroup, groups);
    var axis = "";
    for (var i = 0; i < groups.length; i++)
        if (groups[i].group === canonical) { axis = groups[i].axis; break; }
    var facetKey = axis.length ? state.filterKey : "";

    // Synchronous native invokable — call it and pass the result straight to done(), no
    // XHR/callback indirection (unlike Tankoban's async MAL path).
    var native = bc.discoverPage(state.catalogKey, axis, facetKey, deps.showExplicit, offset, limit);
    var rows = (native && native.items) || [];
    var items = [];
    for (var j = 0; j < rows.length; j++) items.push(normalizeBook(rows[j]));

    var exhausted = !native || native.exhausted === true;
    var warning = (native && native.warning) || "";
    // a stale/offline snapshot surfaces the shell's offline notice honestly (never invented —
    // only when the service itself reports it, and only when native didn't already warn).
    if (!warning.length && bc.offline === true) warning = OFFLINE_WARNING;

    done(generation, {
        items: items,
        nextCursor: exhausted ? null : (native ? native.nextOffset : null),
        exhausted: exhausted,
        freshness: (native && native.freshness) || "bundled",
        warning: warning
    });
}

function fetchExtensionPage(deps, state, cursor, generation, done) {
    var catalog = DiscoverApi.catalogByKey(deps.extensions, "book", state.catalogKey);
    if (!catalog) {
        done(generation, { items: [], nextCursor: null, exhausted: true, freshness: "bundled", warning: "" });
        return;
    }
    var selections = DiscoverApi.selectionsForFilter(catalog, state.filterGroup, state.filterKey);
    var skip = (cursor !== null && cursor !== undefined) ? cursor : 0;
    DiscoverApi.loadPage(catalog, selections, skip, function(metas) {
        var rows = metas || [];
        var items = [];
        for (var i = 0; i < rows.length; i++) {
            var card = normalizeExtensionBook(rows[i], catalog);
            if (Policy.visible("biblio", card.raw, deps.showExplicit)) items.push(card);
        }
        var next = items.length ? (skip + items.length) : null;
        done(generation, {
            items: items,
            nextCursor: next,
            exhausted: items.length === 0,
            freshness: "bundled",
            warning: ""
        });
    });
}

function fetchPage(deps, state, cursor, generation, done) {
    var key = state.catalogKey || "";
    if (key.indexOf("|book|") >= 0) fetchExtensionPage(deps, state, cursor, generation, done);
    else fetchBuiltinPage(deps, state, cursor, generation, done);
}

// --- the adapter factory ---
// Dependencies are passed explicitly because .pragma library cannot see QML context
// properties. The page wrapper calls create() and passes the result to DiscoverBrowser.
function create(biblioCatalog, extensions, showExplicit) {
    var deps = {
        biblioCatalog: biblioCatalog,
        extensions: extensions || [],
        showExplicit: showExplicit === true
    };
    return {
        types: function() { return [{ key: "book", label: "Books" }]; },
        catalogs: function(type) { return type === "book" ? catalogsForType(deps.extensions) : []; },
        filters: function(type, catalogKey) {
            return type === "book" ? filterGroupsFor(deps.biblioCatalog, deps.showExplicit) : [];
        },
        defaultCatalog: function(type) { return "popular"; },
        resolvePin: function(pin) { return resolvePin(pin, deps); },
        fetchPage: function(state, cursor, generation, done) { fetchPage(deps, state, cursor, generation, done); }
    };
}
