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
function setData(obj) { return _ingest(obj); }

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
                   locgId: "locg:" + s.locg_id, publisher: s.publisher || "" });
    }
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
