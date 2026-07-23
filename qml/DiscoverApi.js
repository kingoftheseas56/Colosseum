// DiscoverApi.js — pure derivations for the Discover page (Stage 1, arc 2026-07-23).
// Computes the three pickers and the fetch URLs from Extensions.installed() manifests.
// A .pragma library can't see context properties: the PAGE passes the installed list
// into every call. Fetch-free except loadPage (which rides AddonClient's XHR lane).
.pragma library
.import "AddonClient.js" as AddonClient

var TYPE_ORDER = ["movie", "series", "anime"];
var PAGE_FETCH_TIMEOUT_GUARD = 0; // reserved; AddonClient owns timeouts

function typeLabel(t) {
    if (t === "movie") return "Movie";
    if (t === "series") return "Series";
    if (t === "anime") return "Anime";
    return String(t).charAt(0).toUpperCase() + String(t).slice(1);
}

// union of catalog types across enabled addons, house order first, rest alphabetical
function typesFor(installed) {
    var seen = {}, rest = [];
    for (var i = 0; i < (installed || []).length; i++) {
        var e = installed[i];
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
    var specs = AddonClient.discoverCatalogSpecs(installed, type);
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
    AddonClient.fetchCatalogUrl(urlFor(catalog, selections, skip), done);
}
