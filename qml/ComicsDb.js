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

// _engine — the ComicsCatalog C++ context property, once handed over by ComicsDbLoader/
// TankobanWorld (P4 seam, 2026-07-18). When set, every public fn below delegates to the
// curated_* SQLite tables instead of the baked gen.js blob; setData()'s _db path stays as
// the fixture/test lane and is untouched. Engine takes priority when both are present.
var _engine = null;
function setEngine(e) {
    _engine = (e && e.curatedReady && e.curatedReady()) ? e : null;
    _byTitle = null;
    return _engine !== null;
}

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

// setData(obj) — the FIXTURE/TEST path: hand a parsed comics_db object straight in (the logic
// harness ships an inline fixture through here). The shipped catalog rides the ComicsCatalog
// SQLite engine via setEngine() instead (P4, 2026-07-18 — the baked gen.js wrapper retired);
// load(url) stays as the hosted path for a future static-host move.
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

function ready() { return _engine !== null || _db !== null; }

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
    if (_engine) {
        var hit = _engine.curatedByNorm(_normTitle(title));
        return (hit && hit.locgId !== undefined) ? { locgId: "locg:" + hit.locgId, title: hit.title,
                       cover: hit.cover || "", publisher: hit.publisher || "" } : null;
    }
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
    if (_engine) {
        var rows = _engine.curatedRanked();
        var eout = [];
        for (var ri = 0; ri < rows.length; ri++) {
            var r = rows[ri];
            eout.push({ rank: r.rank, caption: r.title, title: r.title, cover: r.cover || "",
                        locgId: "locg:" + r.locgId, publisher: r.publisher || "",
                        year: r.year || 0, genres: r.genres ? r.genres.split(",") : [] });
        }
        return eout;
    }
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
    if (_engine) {
        var rows = _engine.curatedGenreShelves(maxCovers || 8);
        var eout = [];
        for (var ri = 0; ri < rows.length; ri++) {
            var r = rows[ri];
            eout.push({ name: r.name, count: r.count, covers: r.covers || [] });
        }
        return eout;
    }
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
    if (_engine) return (series(locgId) || {}).editions || [];
    var id = String(locgId || "").replace(/^locg:/, "");
    var s = _bySid[id];
    return (s && s.editions) ? s.editions : [];
}

// The full series record (title, publisher, cover, editions) or null.
function series(locgId) {
    if (_engine) {
        var eid = String(locgId || "").replace(/^locg:/, "");
        var m = _engine.curatedSeries(eid);
        if (!m || m.locgId === undefined) return null;
        var srcEds = m.editions || [];
        var mappedEds = [];
        for (var i = 0; i < srcEds.length; i++) {
            var e = srcEds[i];
            mappedEds.push({
                title: e.title || "",
                display_title: e.displayTitle || "",
                format: e.format || "",
                collects: e.collects || "",
                isbn: e.isbn || "",
                pages: e.pages || 0,
                published: e.published || "",
                locg_comic_id: e.chid || "",
                cover: e.cover || "",
                available: !!e.available,
                getcomics_post: e.getcomicsPost || "",
                creators: e.creators || "",
                description: e.description || "",
                sources: e.sources || []
            });
        }
        return {
            title: m.title || "", year: m.year || 0, slug: m.slug || "",
            locg_id: m.locgId, publisher: m.publisher || "", cover: m.cover || "",
            synopsis: m.synopsis || "", editions: mappedEds,
            coverage: m.coverage || {}
        };
    }
    var id = String(locgId || "").replace(/^locg:/, "");
    return _bySid[id] || null;
}

// A series' "Also on GetComics" rail: GetComics posts the id-anchored attachment
// (parser+attachment arc 2026-07-16) proved belong to this series but no edition
// auto-wired — bundles, compendium packs, story-title posts. Availability was
// verified at fold time. [] if none. (Distinct from the per-edition torrent
// picker in ComicTorrentSourcesPage — this is series-level GetComics downloads.)
function sources(locgId) {
    var s = series(locgId);
    if (!s) return [];
    if (!_engine) return s.sources || [];
    var out = [];
    var eds = s.editions || [];
    for (var i = 0; i < eds.length; i++) {
        var rows = eds[i].sources || [];
        for (var j = 0; j < rows.length; j++) {
            var source = rows[j];
            source.editionId = eds[i].locg_comic_id || "";
            source.editionTitle = eds[i].display_title || eds[i].title || "";
            out.push(source);
        }
    }
    return out;
}

function coverage(locgId) {
    var s = series(locgId);
    return (s && s.coverage) ? s.coverage : {};
}

// The downloadable GetComics post URL for an edition, or null. The app re-parses the signed /dls/
// link fresh at click (those expire), so we store/return only the stable POST url here.
function downloadPost(edition) {
    return (edition && edition.available && edition.getcomics_post) ? edition.getcomics_post : null;
}

// Whether the series contains at least one edition with a real GetComics source.
// This is intentionally derived from the same downloadPost() truth used by the ledger.
function hasDownloadableEdition(locgId) {
    if (_engine) return _engine.curatedHasDownloadable(String(locgId || "").replace(/^locg:/, ""));
    var rows = editions(locgId);
    for (var i = 0; i < rows.length; i++) {
        if (downloadPost(rows[i]) !== null) return true;
    }
    return false;
}
