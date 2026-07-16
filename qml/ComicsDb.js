// ComicsDb.js — the app's comics brain, OFFLINE.
//
// Reads the weekly-built `comics_db.json` sidecar (produced by scripts/comics_brain/
// build_comics_db.py): RCO rank -> LOCG collected editions -> GetComics availability -> PRH/S&S
// enrichment. The app does NO live LOCG/GetComics resolution — it just reads this file. That
// centralizes the fragile, CF-walled, mirror-rotting scraping into one weekly job off the user's
// machine (Hemanth's call 2026-07-13); the app stays instant + offline.
//
// DEPLOYMENT SEAM: everything loads through load(source, done). `source` is a local file URL today
// (Qt.resolvedUrl("../resources/comics_db.json"), injected by Main.qml) and a hosted URL tomorrow —
// local->hosted is a one-line source swap + a cache, not a rewrite.
//
// Pure/testable: fetchFn is injected (Main.qml wires real XHR; tests inject a fake) — the
// injected-clock lesson from LocgApi.js.
.pragma library

var _db = null;                 // parsed comics_db.json (null until load() succeeds)
var _bySid = {};                // "<locg_id>" -> series record, for O(1) series lookup

var fetchFn = null;             // injected: function(url, cb(bodyOrNull)); default = XHR below
var DEFAULT_SOURCE = "";        // Main.qml injects Qt.resolvedUrl("../resources/comics_db.json")

function _defaultFetch(url, cb) {
    // SYNCHRONOUS read (open(..., false)). The sidecar is a small local file read ONCE at startup;
    // sync guarantees ready() is true the instant load() returns — before any world/series page is
    // created — so the shelf's `ComicsDb.ready() ? … : curated` binding (evaluated once, non-
    // reactive) sees the loaded DB instead of racing an async callback. (A hosted URL later would
    // switch this back to async + a reactive property.)
    try {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", url, false);
        xhr.send();
        // A file:// read reports status 0 on success in Qt's XHR; a hosted URL reports 200.
        var ok = (xhr.status === 0 || (xhr.status >= 200 && xhr.status < 300));
        cb(ok ? xhr.responseText : null);
    } catch (e) {
        cb(null);
    }
}

// _ingest(obj) — accept a parsed comics_db object, index it. Returns true on success.
function _ingest(d) {
    if (!d || !Array.isArray(d.series)) return false;
    _db = d;
    _bySid = {};
    for (var i = 0; i < d.series.length; i++) {
        var s = d.series[i];
        if (s && s.locg_id) _bySid[String(s.locg_id)] = s;
    }
    return true;
}

// setData(obj) — the LOCAL path: the weekly build ships a comics_db.gen.js (a .pragma-library
// wrapper) that Main.qml imports and hands here. No file read — QML's import system delivers the
// data reliably (Qt's XHR CANNOT read local files at runtime; the app's own tests ship data as
// generated .js for exactly this reason). This is the sidecar today; load(url) is the hosted path.
function setData(obj) { _byTitle = null; return _ingest(obj); }

// load(source, done(ok)) — the HOSTED path (later): fetch + parse a URL. Kept for when the DB moves
// to a static host; not used for the local sidecar (see setData).
function load(source, done) {
    var src = source || DEFAULT_SOURCE;
    (fetchFn || _defaultFetch)(src, function(body) {
        if (!body) { if (done) done(false); return; }
        try {
            if (done) done(_ingest(JSON.parse(body)));
        } catch (e) {
            if (done) done(false);
        }
    });
}

function ready() { return _db !== null; }

// ── normalized-title lookup: routes any GetComics-era series open to OUR series
//    view when the catalog carries it (Hemanth 2026-07-15: the DB view is THE
//    series view; the GC shelf is the fallback for series we don't carry). ──
var _byTitle = null;
function _normTitle(s) {
    s = String(s || "").toLowerCase();
    s = s.replace(/['’]s/g, "");
    s = s.replace(/[^a-z0-9 ]+/g, " ").replace(/\s+/g, " ").trim();
    return s.replace(/^(the|a|an) /, "");
}
function seriesByTitle(title) {
    if (!_db) return null;
    if (!_byTitle) {
        _byTitle = {};
        for (var i = 0; i < _db.series.length; i++) {
            var s = _db.series[i];
            if (!s.locg_id) continue;
            var key = _normTitle(s.title);
            if (key && !_byTitle[key]) _byTitle[key] = s;   // first hit = highest ranked
        }
    }
    var hit = _byTitle[_normTitle(title)];
    return hit ? { locgId: "locg:" + hit.locg_id, title: hit.title,
                   cover: hit.cover || "", publisher: hit.publisher || "" } : null;
}

// The ranked shelf: [{rank, title, cover, locgId, publisher}] in rank order. `locgId` carries the
// "locg:<id>" form the rest of the app (openComicSeries/LocgApi) already speaks.
function rankedSeries() {
    if (!_db) return [];
    var out = [];
    for (var i = 0; i < _db.series.length; i++) {
        var s = _db.series[i];
        if (!s.locg_id) continue;               // unmatched seed rows have no series to open
        // caption == title so TrendingTop10's {caption, cover} tile renders these directly;
        // locgId is the extra the click handler needs to open the series.
        out.push({ rank: s.rank, caption: s.title, title: s.title, cover: s.cover || "",
                   locgId: "locg:" + s.locg_id, publisher: s.publisher || "",
                   year: s.year || 0,          // run year — disambiguates same-title runs (JL 2011/2016/2018)
                   genres: s.genres || [] });
    }
    return out;
}

// Genre shelves for the Explore mosaic: every genre the catalog carries, biggest
// shelf first, with a small cover pool for the box art. Series without genres
// simply don't shelve (they stay reachable via rank rows + search).
function genreShelves(maxCovers) {
    if (!_db) return [];
    var caps = maxCovers || 8;
    var by = {};
    for (var i = 0; i < _db.series.length; i++) {
        var s = _db.series[i];
        if (!s.locg_id) continue;
        var gs = s.genres || [];
        for (var g = 0; g < gs.length; g++) {
            var shelf = by[gs[g]] || (by[gs[g]] = { name: gs[g], count: 0, covers: [] });
            shelf.count += 1;
            if (s.cover && shelf.covers.length < caps) shelf.covers.push(s.cover);
        }
    }
    var out = [];
    for (var name in by) out.push(by[name]);
    out.sort(function(a, b) { return b.count - a.count || (a.name < b.name ? -1 : 1); });
    return out;
}

// A series' collected editions, by "locg:<id>" or bare id. [] if unknown.
function editions(locgId) {
    var id = String(locgId || "").replace(/^locg:/, "");
    var s = _bySid[id];
    return (s && s.editions) ? s.editions : [];
}

// The full series record (title, publisher, cover, editions) or null.
function series(locgId) {
    var id = String(locgId || "").replace(/^locg:/, "");
    return _bySid[id] || null;
}

// The downloadable GetComics post URL for an edition, or null. The app re-parses the signed /dls/
// link fresh at click (those expire), so we store/return only the stable POST url here.
function downloadPost(edition) {
    return (edition && edition.available && edition.getcomics_post) ? edition.getcomics_post : null;
}

// Whether the series contains at least one edition with a real GetComics source.
// This is intentionally derived from the same downloadPost() truth used by the ledger.
function hasDownloadableEdition(locgId) {
    var rows = editions(locgId);
    for (var i = 0; i < rows.length; i++) {
        if (downloadPost(rows[i]) !== null) return true;
    }
    return false;
}
