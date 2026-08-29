// DiscoverApi.js — pure derivations for the Discover page (Stage 1, arc 2026-07-23).
// Computes the three pickers and the fetch URLs from Extensions.installed() manifests.
// A .pragma library can't see context properties: the PAGE passes the installed list
// into every call. Fetch-free except loadPage (which rides AddonClient's XHR lane).
.pragma library
.import "AddonClient.js" as AddonClient

var TYPE_ORDER = ["movie", "series", "anime"];
var PAGE_FETCH_TIMEOUT_GUARD = 0; // reserved; AddonClient owns timeouts

// Cinemeta's SEED manifest (ExtensionsStore::seed) ships WITHOUT a catalogs[] —
// the tab shelves hit Cinemeta's HTTP catalog endpoint directly, so the seed
// never needed one. Discover derives its whole picker PURELY from manifest
// catalogs, so the core Cinemeta row would be catalog-less and the wall empty.
// Synthesize Cinemeta's two live "Popular" catalogs onto that row before deriving.
// Genre options copied VERBATIM from https://v3-cinemeta.strem.io/manifest.json
// (fetched 2026-07-23). Fires ONLY for a core Cinemeta entry that truly lacks
// catalogs — a re-installed Cinemeta (real manifest) or any other addon is left
// untouched. C++-free per the QML-only stage rule (plan Task 7 Step 3).
var CINEMETA_MOVIE_GENRES = ["Action", "Adventure", "Animation", "Biography",
    "Comedy", "Crime", "Documentary", "Drama", "Family", "Fantasy", "History",
    "Horror", "Mystery", "Romance", "Sci-Fi", "Sport", "Thriller", "War", "Western"];
var CINEMETA_SERIES_GENRES = ["Action", "Adventure", "Animation", "Biography",
    "Comedy", "Crime", "Documentary", "Drama", "Family", "Fantasy", "History",
    "Horror", "Mystery", "Romance", "Sci-Fi", "Sport", "Thriller", "War", "Western",
    "Reality-TV", "Talk-Show", "Game-Show"];

function _cinemetaFallbackCatalogs() {
    return [
        { type: "movie", id: "top", name: "Popular",
          extra: [{ name: "genre", options: CINEMETA_MOVIE_GENRES }, { name: "skip" }] },
        { type: "series", id: "top", name: "Popular",
          extra: [{ name: "genre", options: CINEMETA_SERIES_GENRES }, { name: "skip" }] }
    ];
}

function _isCinemetaCore(e) {
    return e && e.core === true &&
        (e.id === "com.linvo.cinemeta" || /cinemeta/i.test(String(e.transportUrl || "")));
}

// Cinemeta's addon origin (v3-cinemeta.strem.io) answers a catalog GET with a 307
// redirect to its CDN, and Qt's XHR does NOT follow it (empty body → blank wall).
// Point the synthetic Cinemeta catalogs at the CDN directly — the exact endpoint
// the Theatre tab shelves already use (cinemeta-catalogs.strem.io/top), which serves
// genre filters and skip paging. Verified live 2026-06-24.
var CINEMETA_CDN_TRANSPORT = "https://cinemeta-catalogs.strem.io/top/manifest.json";

// The installed list with the Cinemeta seed's missing catalogs filled in. Every
// entry passes through untouched EXCEPT a catalog-less core Cinemeta row, which
// gets a shallow copy carrying the synthetic catalogs (originals never mutated).
function _effectiveInstalled(installed) {
    var out = [];
    for (var i = 0; i < (installed || []).length; i++) {
        var e = installed[i];
        if (!e) continue;
        var m = e.manifest || {};
        var cats = m.catalogs || [];
        if (!cats.length && _isCinemetaCore(e)) {
            var m2 = {};
            for (var k in m) m2[k] = m[k];
            m2.catalogs = _cinemetaFallbackCatalogs();
            var e2 = {};
            for (var kk in e) e2[kk] = e[kk];
            e2.manifest = m2;
            e2.transportUrl = CINEMETA_CDN_TRANSPORT;   // dodge v3-cinemeta's unfollowed 307
            out.push(e2);
        } else {
            out.push(e);
        }
    }
    return out;
}

function typeLabel(t) {
    if (t === "movie") return "Movie";
    if (t === "series") return "Series";
    if (t === "anime") return "Anime";
    return String(t).charAt(0).toUpperCase() + String(t).slice(1);
}

// union of catalog types across enabled addons, house order first, rest alphabetical
function typesFor(installed) {
    var eff = _effectiveInstalled(installed);
    var seen = {}, rest = [];
    for (var i = 0; i < eff.length; i++) {
        var e = eff[i];
        if (!e || e.enabled !== true) continue;
        var cats = (e.manifest && e.manifest.catalogs) || [];
        for (var j = 0; j < cats.length; j++)
            if (cats[j] && cats[j].type && AddonClient.discoverBrowsable(cats[j]))
                seen[cats[j].type] = true;
    }
    var out = [];
    for (var k = 0; k < TYPE_ORDER.length; k++)
        if (seen[TYPE_ORDER[k]]) { out.push(TYPE_ORDER[k]); delete seen[TYPE_ORDER[k]]; }
    for (var t in seen) rest.push(t);
    rest.sort();
    return out.concat(rest);
}

// picker model: [{key, title, addonName, transportUrl, type, catalogId, extra, genres, core}]
function catalogsFor(installed, type) {
    var specs = AddonClient.discoverCatalogSpecs(_effectiveInstalled(installed), type);
    var out = [];
    for (var i = 0; i < specs.length; i++) {
        var s = specs[i];
        out.push({
            key: s.transportUrl + "|" + s.type + "|" + s.catalogId,
            title: s.title, addonName: s.extName,
            transportUrl: s.transportUrl, type: s.type, catalogId: s.catalogId,
            extra: s.extra, genres: s.genres, core: s.core
        });
    }
    return out;
}

// the selected catalog's user-facing filters: [{name, label, options, isRequired}]
// skip/search never surface as pickers.
function extrasFor(catalog) {
    var out = [];
    var extras = (catalog && catalog.extra) || [];
    for (var i = 0; i < extras.length; i++) {
        var x = extras[i];
        if (!x || !x.name || x.name === "skip" || x.name === "search") continue;
        var opts = x.options || (x.name === "genre" ? (catalog.genres || []) : []);
        if (!opts.length) continue;
        out.push({ name: x.name, label: typeLabel(x.name), options: opts,
                   isRequired: x.isRequired === true });
    }
    // legacy manifests: genres[] with no extra entry at all
    if (!out.length && catalog && catalog.genres && catalog.genres.length)
        out.push({ name: "genre", label: "Genre", options: catalog.genres, isRequired: false });
    return out;
}

// {extraName: value|null} — required extras auto-pick their first option (spec 3.2)
function defaultSelections(extras) {
    var out = {};
    for (var i = 0; i < (extras || []).length; i++)
        out[extras[i].name] = extras[i].isRequired ? extras[i].options[0] : null;
    return out;
}

function urlFor(catalog, selections, skip) {
    var pairs = [];
    for (var name in (selections || {}))
        pairs.push([name, selections[name]]);
    return AddonClient.catalogUrl(catalog.transportUrl, catalog.type, catalog.catalogId,
                                  pairs, skip || 0);
}

// a See-all pin {transportUrl, type, catalogId, addonName} against the live registry:
// {catalog} when installed, {missing:true, addonName, transportUrl} when not.
function resolvePin(installed, pin) {
    var cats = catalogsFor(installed, pin.type);
    for (var i = 0; i < cats.length; i++)
        if (cats[i].transportUrl === pin.transportUrl && cats[i].catalogId === pin.catalogId)
            return { missing: false, catalog: cats[i] };
    return { missing: true, addonName: pin.addonName || "", transportUrl: pin.transportUrl };
}

// one page fetch (transport rides AddonClient). done(metas) — [] on miss/error.
function loadPage(catalog, selections, skip, done) {
    return AddonClient.fetchCatalogUrl(urlFor(catalog, selections, skip), done);
}

// ───────────────── shared-shell adapter translation (Task 3, arc 2026-08-01) ─────────────────
// The Discover shell (DiscoverBrowser.qml) speaks a WORLD-NEUTRAL contract. These helpers
// re-shape what the derivations ABOVE already return into that contract — they add NO new
// transport and change NO existing behaviour (same Cinemeta fallback, same URLs, same
// ordering, same required-extra defaults). The Theatre wrapper wires them into its adapter.

// typesFor(...) -> the shell's [{ key, label }] (house order preserved).
function shellTypes(installed) {
    var ts = typesFor(installed);
    var out = [];
    for (var i = 0; i < ts.length; i++) out.push({ key: ts[i], label: typeLabel(ts[i]) });
    return out;
}

// catalogsFor(...) -> the shell's catalog descriptors [{ key, title, sourceKind, section, attribution }].
// Sectioning mirrors the old catalog menu EXACTLY: a core row sits under its own addon name,
// everything else under "Your addons"; attribution is always the owning addon.
function shellCatalogs(installed, type) {
    var cats = catalogsFor(installed, type);
    var out = [];
    for (var i = 0; i < cats.length; i++) {
        var c = cats[i];
        out.push({
            key: c.key, title: c.title,
            sourceKind: c.core ? "builtin" : "extension",
            section: c.core ? c.addonName : "Your addons",
            attribution: c.addonName
        });
    }
    return out;
}

// the built-in default catalogue key for a type (the FIRST catalogue, installed order —
// core Cinemeta first — matching the old setType -> setCatalog(0)).
function shellDefaultCatalog(installed, type) {
    var cats = catalogsFor(installed, type);
    return cats.length ? cats[0].key : "";
}

// extrasFor(catalog) -> the shell's filter groups [{ group, options: [{ key, label }] }].
// One group per user-facing extra; each option key equals its label (the genre value).
function shellFilters(catalog) {
    var extras = extrasFor(catalog);
    var out = [];
    for (var i = 0; i < extras.length; i++) {
        var opts = [];
        for (var j = 0; j < extras[i].options.length; j++)
            opts.push({ key: extras[i].options[j], label: extras[i].options[j] });
        out.push({ group: extras[i].label, options: opts });
    }
    return out;
}

// the full DiscoverApi catalog object for a shell catalog key (transportUrl|type|catalogId).
function catalogByKey(installed, type, key) {
    var cats = catalogsFor(installed, type);
    for (var i = 0; i < cats.length; i++)
        if (cats[i].key === key) return cats[i];
    return null;
}

// resolvePin(...) -> the shell's { missing, type, catalogKey, filterGroup, filterKey, missingName }.
// On a hit the found catalogue's key is returned; on a miss the pin still carries the type so the
// shell can fall to that type's built-in default.
function shellResolvePin(installed, pin) {
    var res = resolvePin(installed, pin);
    if (res.missing)
        return { missing: true, type: pin.type, catalogKey: "", filterGroup: "", filterKey: "",
                 missingName: res.addonName || pin.addonName || "" };
    return { missing: false, type: res.catalog.type, catalogKey: res.catalog.key,
             filterGroup: "", filterKey: "", missingName: "" };
}

// The FULL selections map for a fetch, rebuilt from the catalogue's extras so REQUIRED extras
// are ALWAYS sent (defaultSelections auto-picks them), then overridden by the shell's single
// active filter. The shell surfaces ONE filter (filterGroup is the extra's LABEL, as shellFilters
// emits it) — map it back to the extra name. Clearing an OPTIONAL extra sets it null; a required
// extra keeps its auto-picked default regardless of what the single-filter UI shows. THIS is where
// Theatre's required-extra fetch behaviour is preserved under the collapsed filter UI.
function selectionsForFilter(catalog, filterGroup, filterKey) {
    var extras = extrasFor(catalog);
    var sel = defaultSelections(extras);
    for (var i = 0; i < extras.length; i++) {
        if (extras[i].label !== filterGroup) continue;
        if (filterKey && filterKey.length) sel[extras[i].name] = filterKey;
        else if (!extras[i].isRequired) sel[extras[i].name] = null;
        break;
    }
    return sel;
}

// a Stremio/Cinemeta meta preview -> the shell's normalized card. The DISPLAYED values match
// the old Discover card exactly (title/year/rating/cover fallbacks in the SAME order); `raw`
// keeps the original meta so the Theatre wrapper can re-emit it untouched to the detail page.
function normalizeMeta(meta, type) {
    var m = meta || {};
    return {
        id: (m.id !== undefined && m.id !== null) ? m.id : "",
        type: m.type || type,
        title: m.title || m.caption || m.name || "",
        cover: m.cover || m.poster || "",
        year: m.releaseInfo || m.year || "",
        rating: m.imdbRating || "",
        format: "",
        publisher: "",
        availability: true,
        explicit: false,
        raw: m
    };
}
