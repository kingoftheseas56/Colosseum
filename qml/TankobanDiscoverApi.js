// TankobanDiscoverApi.js — the Tankoban adapter for the shared Discover shell.
// (Task 6, arc 2026-08-01.)
//
// The shell (DiscoverBrowser.qml) is WORLD-NEUTRAL: it speaks a small adapter
// contract (types/catalogs/filters/defaultCatalog/resolvePin/fetchPage) and renders
// normalized cards. This file IS the Tankoban adapter — it owns the catalogue
// descriptors, the filter shape, normalization, the See-all pin resolution, the
// local-first page delivery, the non-blocking Jikan refresh, and the extension
// catalogue seam. It performs NO acquisition and owns NO series-page UI (the page
// wrapper routes normalized cards to the existing Manga/Comics series doors).
//
// A .pragma library CANNOT see QML context properties, so dependencies are passed
// explicitly into create(): the MalCatalog and ComicsCatalog objects, the extension
// registry, the global showExplicit flag, and (optionally) an XMLHttpRequest factory
// (the page injects the real one; the harness injects a fake).
.pragma library
.import "ExplicitContentPolicy.js" as Policy

// Exposed for a static-contract grep (the harness asserts no Comic Vine / Metron
// runtime dependency, no direct download action lives in this file).
var SOURCE = "tankoban-discover-adapter";

// --- catalogue descriptors (spec 3.3) ---
// Manga launch set: Trending, Popular, Top Rated, New Releases.
// Comics launch set: Popular, New Releases, Most Stocked, All Series.
var MANGA_CATALOGS = [
    { key: "trending",     title: "Trending",      sourceKind: "builtin" },
    { key: "popular",      title: "Popular",       sourceKind: "builtin" },
    { key: "top-rated",    title: "Top Rated",     sourceKind: "builtin" },
    { key: "new-releases", title: "New Releases",  sourceKind: "builtin" }
];
var COMICS_CATALOGS = [
    { key: "popular",       title: "Popular",       sourceKind: "builtin" },
    { key: "new-releases",  title: "New Releases",  sourceKind: "builtin" },
    { key: "most-stocked",  title: "Most Stocked",  sourceKind: "builtin" },
    { key: "all",           title: "All Series",    sourceKind: "builtin" }
];
var BUILTIN_SECTION = "Tankoban";
var BUILTIN_ATTRIBUTION = "Tankoban built-in catalogue";

// in-session live-refresh cache (spec 5.4). Keyed by catalog+filter+explicit flag so a
// refresh is deduplicated per state (a sfw=true response never serves an opted-in
// session); liveInFlight prevents duplicate concurrent requests for the same key. A
// fresh entry (< CACHE_MS) is merged into the next bundled delivery; while one exists
// (or a request is in flight) no new refresh fires.
var liveCache = {};
var liveInFlight = {};
var CACHE_MS = 15 * 60 * 1000;
function liveCacheKey(state, showExplicit) {
    return state.catalogKey + "|" + state.filterGroup + "|" + state.filterKey
         + "|" + (showExplicit ? "x" : "s");
}

// Jikan endpoint base. sfw derives from the global preference (spec 5.3/6.3):
// showExplicit=false -> sfw=true (the conservative default); showExplicit=true ->
// sfw=false so an opted-in user can see explicit MAL titles in the wall.
var JIKAN_TOP = "https://api.jikan.moe/v4/top/manga";
var JIKAN_TRENDING_FALLBACK_WARNING = "Trending is using the latest popularity snapshot.";

// Build the Jikan URL for a catalogue + page offset. Pure: the harness calls it
// directly to assert the sfw derivation and endpoint pin.
function jikanUrl(catalogId, offset, showExplicit) {
    var sfw = showExplicit ? "false" : "true";
    // Popular/Top-Rated map to /top/manga with a type filter; Trending would need a
    // real /seasons or /watch/promotions endpoint with comparable snapshots, which we
    // do not have yet, so it is handled by the bundled fallback (no fake momentum URL).
    var order = (catalogId === "top-rated") ? "by=score" : "by=popularity";
    var page = Math.floor((offset || 0) / 24) + 1;   // Jikan is 1-indexed, ~24/page
    return JIKAN_TOP + "?type=manga&order_by=" + (catalogId === "top-rated" ? "score" : "members")
         + "&sort=desc&sfw=" + sfw + "&page=" + page;
}

// --- normalization (spec 3.5) ---
// A Jikan-shaped MAL row -> the shell's normalized card. Format preserves Manga /
// Manhwa / Manhua (origin shows on the card metadata line, never as a separate
// launch filter). Availability defaults false; the adapter enriches it later when a
// live refresh carries a stable MAL id we already hold.
function normalizeManga(row) {
    var r = row || {};
    return {
        id: (r.mal_id !== undefined && r.mal_id !== null) ? String(r.mal_id) : "",
        type: "manga",
        title: r.title || r.title_english || "",
        cover: r.cover || r.images && r.images.jpg && r.images.jpg.image_url || "",
        year: r.year || 0,
        rating: r.score || 0,
        format: r.type || "Manga",          // Manga / Manhwa / Manhua / Novel etc.
        publisher: "",
        availability: r.availability === true,
        explicit: r.explicit === true,
        raw: r
    };
}

// A house-shaped ComicsCatalog row -> the shell's normalized card. Comics carry no
// manga-style type, so format is empty; publisher + year + genres are preserved.
function normalizeComic(row) {
    var r = row || {};
    return {
        id: r.locgId || "",
        type: "comics",
        title: r.title || "",
        cover: r.cover || "",
        year: r.year || 0,
        rating: 0,                          // house ranking is never a public rating (spec 5.1)
        format: "",
        publisher: r.publisher || "",
        availability: r.availability === true,
        explicit: r.explicit === true,
        raw: r
    };
}

// Canonical-identity merge (spec 5.3). A live result with a STABLE MAL id replaces
// the bundled row for that id (enriching cover/score/members); a live result with no
// stable id is DROPPED — it never displaces a bundled row by title similarity alone.
function mergeByIDentity(bundled, live) {
    var byId = {};
    for (var i = 0; i < bundled.length; i++) byId[bundled[i].id] = bundled[i];
    for (var j = 0; j < (live || []).length; j++) {
        var l = live[j];
        var id = l.mal_id !== undefined ? String(l.mal_id) : (l.id !== undefined ? String(l.id) : "");
        if (!id.length) continue;          // no stable identity -> drop, never title-match
        var merged = normalizeManga(l);
        if (byId[id]) {
            // preserve the bundled card's acquisition/availability; overlay live metadata
            merged.availability = byId[id].availability;
        }
        byId[id] = merged;
    }
    var out = [];
    for (var k in byId) out.push(byId[k]);
    return out;
}

// The WALL's merge: overlay live metadata onto the bundled cards IN PLACE by stable
// MAL id. The bundled ORDER is preserved — covers never move under the user's hand
// (spec 5.4) — and availability keeps the bundled truth (acquisition is local
// knowledge Jikan does not have). A live row with no stable id touches nothing.
// mergeByIDentity above remains the full replace-merge for callers that want the
// live SET; the wall wants enrichment without reordering.
function enrichByIdentity(cards, liveRows) {
    var byId = {};
    for (var i = 0; i < (liveRows || []).length; i++) {
        var l = liveRows[i];
        var id = (l && l.mal_id !== undefined && l.mal_id !== null) ? String(l.mal_id) : "";
        if (id.length) byId[id] = l;
    }
    var out = [];
    for (var j = 0; j < cards.length; j++) {
        var c = cards[j];
        var live = byId[c.id];
        if (!live) { out.push(c); continue; }
        var merged = normalizeManga(live);
        merged.availability = c.availability;   // bundled acquisition truth survives
        out.push(merged);
    }
    return out;
}

// --- filter shape (spec 3.4) ---
// The shell surfaces ONE grouped filter. Manga groups Genres + Demographics; Comics
// groups Genres + Publishers. Each option key is a STABLE lower-case key (distinct
// from its label) so a rename never breaks a saved in-session pin. Facets operate on
// canonical normalized fields, never title keyword guesses.
function stableKey(s) { return String(s || "").toLowerCase().trim() }

// Manga facets arrive as {value, count} (MalCatalog shape); Comics as {key, label, count}.
function buildFilterOptions(facets) {
    var out = [];
    for (var i = 0; i < facets.length; i++) {
        var f = facets[i];
        var key = f.key !== undefined ? stableKey(f.key) : stableKey(f.value);
        var label = f.label || f.value || f.key || "";
        out.push({ key: key, label: label });
    }
    return out;
}

function filtersForType(type, catalog, malCatalog, comicsCatalog, showExplicit) {
    if (!catalog || catalog.length === 0) return [];
    if (type === "manga") {
        if (!malCatalog) return [];
        return [
            { group: "Genres",      options: buildFilterOptions(malCatalog.discoverFilters("genre", showExplicit)) },
            { group: "Demographics", options: buildFilterOptions(malCatalog.discoverFilters("demographic", showExplicit)) }
        ].filter(function(g){ return g.options.length > 0 });
    }
    if (type === "comics") {
        if (!comicsCatalog) return [];
        return [
            { group: "Genres",     options: buildFilterOptions(comicsCatalog.discoverFilters("genre", showExplicit)) },
            { group: "Publishers", options: buildFilterOptions(comicsCatalog.discoverFilters("publisher", showExplicit)) }
        ].filter(function(g){ return g.options.length > 0 });
    }
    return [];
}

// --- extension catalogue seam (spec 4.4) ---
// A Tankoban discovery extension declares a discoverable catalogue. Download-only
// extensions are NEVER surfaced as discovery catalogues — acquisition is a separate
// resource class. Future compatible descriptors land under the "Extensions" section
// after the built-ins.
function extensionCatalogs(type, extensions) {
    var out = [];
    for (var i = 0; i < (extensions || []).length; i++) {
        var e = extensions[i];
        var m = (e && e.manifest) || {};
        var cats = m.catalogs || [];
        for (var j = 0; j < cats.length; j++) {
            var c = cats[j];
            if (!c || c.type !== type) continue;
            if (c.isDiscoverable !== true) continue;      // download-only sources rejected
            out.push({
                key: "ext:" + (m.id || "") + ":" + (c.id || ""),
                title: c.name || c.id || "Extension catalogue",
                sourceKind: "extension",
                section: "Extensions",
                attribution: m.name || m.id || "Extension"
            });
        }
    }
    return out;
}

function catalogsForType(type, extensions) {
    var builtins = (type === "manga") ? MANGA_CATALOGS : (type === "comics") ? COMICS_CATALOGS : [];
    var out = [];
    for (var i = 0; i < builtins.length; i++) {
        out.push({
            key: builtins[i].key,
            title: builtins[i].title,
            sourceKind: builtins[i].sourceKind,
            section: BUILTIN_SECTION,
            attribution: BUILTIN_ATTRIBUTION
        });
    }
    var exts = extensionCatalogs(type, extensions || []);
    for (var k = 0; k < exts.length; k++) out.push(exts[k]);
    return out;
}

// --- pin resolution (spec 3.6) ---
// A pin selects Discover, validates every key against the active adapter, resets
// paging, and scrolls to the wall's start. An invalid/stale filter is DROPPED while
// the valid type/catalogue portion is preserved. A completely unknown type (no
// built-in catalogues) is surfaced as missing so the shell falls to the default +
// shows an explanatory notice.
//
// canonicalFilterGroup: the spec's pin shape (3.6) lists filterGroup values in
// lower-case ("genre"|"demographic"|"publisher"); the shell's internal pipeline
// (validateFilterKey / mangaAxis / comicsAxis / the filter menu) speaks the DISPLAY
// labels ("Genres"|"Demographics"|"Publishers"). This maps either form to the
// canonical display label so a spec-compliant lowercase pin round-trips correctly
// AND a tolerant display-label pin works too. Unknown groups return "" (dropped).
function canonicalFilterGroup(type, group) {
    var g = String(group || "").toLowerCase();
    if (type === "manga") {
        if (g === "genre" || g === "genres") return "Genres";
        if (g === "demographic" || g === "demographics") return "Demographics";
    } else if (type === "comics") {
        if (g === "genre" || g === "genres") return "Genres";
        if (g === "publisher" || g === "publishers") return "Publishers";
    }
    return "";
}

// validateFilterKey: checks the pin's filter against the LIVE facets so a stale key
// (a genre/publisher that no longer exists in the catalogue) is dropped. The catalog
// objects are passed so the validation reads real data, not a hardcoded list.
// Accepts the canonical display label OR the lower-case axis (spec 3.6 pin form).
function validateFilterKey(type, filterGroup, filterKey, malCatalog, comicsCatalog, showExplicit) {
    if (!filterKey || !filterKey.length) return false;
    var canonical = canonicalFilterGroup(type, filterGroup);
    if (!canonical.length) return false;
    var axis = "";
    var source = null;
    if (type === "manga" && malCatalog) {
        axis = (canonical === "Genres") ? "genre" : (canonical === "Demographics") ? "demographic" : "";
        source = malCatalog;
    } else if (type === "comics" && comicsCatalog) {
        axis = (canonical === "Genres") ? "genre" : (canonical === "Publishers") ? "publisher" : "";
        source = comicsCatalog;
    }
    if (!axis || !source) return false;
    var fk = stableKey(filterKey);
    var facets = source.discoverFilters(axis, showExplicit) || [];
    for (var i = 0; i < facets.length; i++) {
        var key = facets[i].key !== undefined ? stableKey(facets[i].key) : stableKey(facets[i].value);
        if (key === fk) return true;
    }
    return false;
}

function resolvePin(pin, extensions) {
    // Standalone fallback (no catalog deps): validates type/catalogue against descriptors
    // only. The adapter's resolvePin closure overrides this to also validate the filter
    // key against the live facets — prefer that path when catalogs are available.
    var p = pin || {};
    var validCats = catalogsForType(p.type, extensions);
    if (!validCats.length)
        return { missing: true, type: p.type, catalogKey: "", filterGroup: "", filterKey: "",
                 missingName: "" };
    var catalogKey = "";
    for (var i = 0; i < validCats.length; i++)
        if (validCats[i].key === p.catalogId) { catalogKey = p.catalogId; break; }
    if (!catalogKey.length) catalogKey = "popular";   // stale catalog -> built-in default
    return { missing: false, type: p.type, catalogKey: catalogKey,
             filterGroup: p.filterGroup || "", filterKey: p.filterKey || "", missingName: "" };
}

// --- local-first page delivery (spec 5.4) ---
// Bundled SQLite results paint IMMEDIATELY via a synchronous callback; the live
// Jikan refresh fires AFTER and never blocks or clears the wall. A refresh may only
// reorder/enrich the wall when the user has not begun direct interaction — that
// interaction fence is owned by the shell, so here we just deliver the refreshed
// page into the cache and let the next reload pick it up.
function mangaAxis(filterGroup) {
    var c = canonicalFilterGroup("manga", filterGroup);
    if (c === "Genres") return "genre";
    if (c === "Demographics") return "demographic";
    return "";
}
function comicsAxis(filterGroup) {
    var c = canonicalFilterGroup("comics", filterGroup);
    if (c === "Genres") return "genre";
    if (c === "Publishers") return "publisher";
    return "";
}

function fetchMangaPage(deps, state, cursor, generation, done) {
    var mal = deps.malCatalog;
    var axis = mangaAxis(state.filterGroup);
    // cursor is the offset (bundled MAL paging is offset-based)
    var offset = (cursor !== null && cursor !== undefined) ? cursor : 0;
    var limit = 24;
    if (!mal) { done(generation, { items: [], nextCursor: null, exhausted: true, freshness: "bundled", warning: "" }); return }

    var native = mal.discoverPage(state.catalogKey, axis, state.filterKey,
                                  deps.showExplicit, offset, limit);
    var rows = (native && native.items) || [];
    var items = [];
    for (var i = 0; i < rows.length; i++) {
        var card = normalizeManga(rows[i]);
        if (Policy.visible("tankoban", card.raw, deps.showExplicit)) items.push(card);
    }
    // NATIVE owns the exhaustion signal (rows < limit at the SQL layer). The old
    // `nextOffset > offset + items.length` comparison read FALSE on a full unfiltered
    // page (both sides equal) and killed paging at page one — it only ever looked
    // alive when the policy filter shortened items below rows. Trust the source.
    var exhausted = !native || native.exhausted === true;
    // Trending honestly falls back: native returns fallbackCatalog:"popular" until
    // two comparable snapshots exist; we surface the warning and DO NOT invent momentum.
    var warning = "";
    if (state.catalogKey === "trending") warning = JIKAN_TRENDING_FALLBACK_WARNING;
    // ── cached live overlay (spec 5.4) ──
    // A fresh in-session Jikan result enriches the bundled first page IN PLACE by
    // stable MAL id — bundled order preserved (covers never jump), availability
    // keeps the bundled truth. This is the "refreshed ordering applies on the next
    // reload" path: the cache was written by an earlier refresh; this reload reads it.
    var freshness = "bundled";
    var cacheKey = liveCacheKey(state, deps.showExplicit);
    if (!axis && offset === 0) {
        var cached = liveCache[cacheKey];
        if (cached && (Date.now() - cached.fetchedAt) < CACHE_MS && cached.items.length) {
            // defense-in-depth: a live overlay could carry a classification the bundled
            // row lacked, so the policy gate re-runs on the enriched cards too.
            items = enrichByIdentity(items, cached.items).filter(function(c) {
                return Policy.visible("tankoban", c.raw, deps.showExplicit);
            });
            freshness = "cached";
        }
    }
    done(generation, {
        items: items,
        nextCursor: exhausted ? null : native.nextOffset,
        exhausted: exhausted,
        freshness: freshness,
        warning: warning
    });

    // ── non-blocking Jikan refresh (spec 5.3/5.4) ──
    // Fires ONLY after the bundled wall landed, and only when no fresh cache entry
    // exists and no request for this state is already in flight (dedup per spec 5.4).
    // A live response merges by stable MAL id on the NEXT reload; it never replaces a
    // bundled row by title similarity and never reorders the wall mid-interaction
    // (the shell owns that fence; the enrich overlay preserves order regardless).
    if (deps.xhrFactory && !axis && offset === 0) {
        var fresh = liveCache[cacheKey]
                    && (Date.now() - liveCache[cacheKey].fetchedAt) < CACHE_MS;
        if (!fresh && !liveInFlight[cacheKey]) {
            liveInFlight[cacheKey] = true;
            try {
                var xhr = deps.xhrFactory();
                var url = jikanUrl(state.catalogKey, offset, deps.showExplicit);
                xhr.open("GET", url);
                xhr.onload = function() {
                    var live = parseJikanResponse(xhr.responseText);
                    if (live.length) liveCache[cacheKey] = { fetchedAt: Date.now(), items: live };
                    delete liveInFlight[cacheKey];
                };
                xhr.onerror = function() { delete liveInFlight[cacheKey]; /* leave bundled wall */ };
                xhr.send();
            } catch (e) { delete liveInFlight[cacheKey]; /* never let a refresh failure blank the wall */ }
        }
    }
}

function parseJikanResponse(text) {
    try {
        var doc = typeof text === "string" ? JSON.parse(text) : text;
        return (doc && doc.data) || [];
    } catch (e) { return []; }
}

function fetchComicsPage(deps, state, cursor, generation, done) {
    var comics = deps.comicsCatalog;
    var axis = comicsAxis(state.filterGroup);
    var offset = (cursor !== null && cursor !== undefined) ? cursor : 0;
    var limit = 24;
    if (!comics) { done(generation, { items: [], nextCursor: null, exhausted: true, freshness: "bundled", warning: "" }); return }

    var native = comics.discoverPage(state.catalogKey, axis, state.filterKey,
                                     deps.showExplicit, offset, limit);
    var rows = (native && native.items) || [];
    var items = [];
    for (var i = 0; i < rows.length; i++) {
        var card = normalizeComic(rows[i]);
        if (Policy.visible("tankoban", card.raw, deps.showExplicit)) items.push(card);
    }
    // NATIVE owns the exhaustion signal (rows < limit at the SQL layer) — see the
    // manga path for why the old nextOffset comparison killed paging at page one.
    var exhausted = !native || native.exhausted === true;
    // Comics have NO live dependency (spec 3.3: no new Comic Vine / Metron runtime dep).
    done(generation, {
        items: items,
        nextCursor: exhausted ? null : native.nextOffset,
        exhausted: exhausted,
        freshness: "bundled",
        warning: ""
    });
}

// --- the adapter factory ---
// Dependencies are passed explicitly because .pragma library cannot see QML context
// properties. The page wrapper calls create() and passes the result to DiscoverBrowser.
function create(malCatalog, comicsCatalog, extensions, showExplicit, xhrFactory) {
    var deps = {
        malCatalog: malCatalog,
        comicsCatalog: comicsCatalog,
        extensions: extensions || [],
        showExplicit: showExplicit === true,
        xhrFactory: xhrFactory || (typeof XMLHttpRequest !== "undefined" ? function(){ return new XMLHttpRequest() } : null)
    };
    return {
        types: function() {
            return [{ key: "manga", label: "Manga" }, { key: "comics", label: "Comics" }];
        },
        catalogs: function(type) { return catalogsForType(type, deps.extensions); },
        filters: function(type, catalogKey) {
            return filtersForType(type, catalogKey, deps.malCatalog, deps.comicsCatalog, deps.showExplicit);
        },
        defaultCatalog: function(type) { return "popular"; },
        resolvePin: function(pin) {
            var p = pin || {};
            var base = resolvePin(p, deps.extensions);
            if (base.missing) return base;
            // validate the filter key against the LIVE facets so a stale genre/publisher
            // is dropped while the valid type/catalogue portion is preserved (spec 3.6).
            // The filter group is canonicalized to the display label the shell's filter
            // menu speaks, so a spec-compliant lowercase pin ("genre"/"publisher") and a
            // tolerant display-label pin ("Genres"/"Publishers") both round-trip.
            var canonical = canonicalFilterGroup(base.type, base.filterGroup);
            var keepFilter = canonical.length > 0
                             && validateFilterKey(base.type, canonical, base.filterKey,
                                                  deps.malCatalog, deps.comicsCatalog, deps.showExplicit);
            return {
                missing: false,
                type: base.type,
                catalogKey: base.catalogKey,
                filterGroup: keepFilter ? canonical : "",
                filterKey: keepFilter ? stableKey(base.filterKey) : "",
                missingName: ""
            };
        },
        fetchPage: function(state, cursor, generation, done) {
            if (state.type === "comics") fetchComicsPage(deps, state, cursor, generation, done);
            else fetchMangaPage(deps, state, cursor, generation, done);
        }
    };
}
