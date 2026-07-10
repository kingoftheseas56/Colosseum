// ComicResolve.js — the attach machine: pairs a LOCG catalogue series with the source slug
// that actually serves its pages. A SUCCESSFUL attach is persisted forever; a no-match is
// remembered ONLY for the session (never persisted — the source's catalog grows weekly, so a miss
// today may attach next launch). Year is a DISAMBIGUATOR, not a hard gate: source titles usually
// carry no year, so a clean title match must attach. Conservative on the wrong end — an ambiguous
// (2+ surviving) candidate is still a NO-match, a wrong comic must never open silently.
// store + searchFn are INJECTED per-source (Main.qml wires QSettings + a source's search fn — was
// xoxo, now GetComics; tests inject fakes) — the machine itself doesn't know which source it's
// pairing — pure/testable, the XoxoApi nowFn lesson.
.pragma library

var store = null;          // injected: { get(key)->string, set(key, value) } — successful attaches ONLY
var searchFn = null;       // injected: source.searchSeries(query, cb(hits, meta)) — source-agnostic
var _miss = {};            // session-only no-match memory — NEVER persisted (the source's catalog grows weekly)

function _norm(t) {
    return String(t).toLowerCase()
        .replace(/\(\d{4}\)/g, "")             // strip year suffixes
        .replace(/\[[^\]]*\]/g, "")            // strip bracketed volume markers: "Batman '66 [I]"
        .replace(/[^a-z0-9]+/g, " ")
        .replace(/\s+/g, " ").trim();
}
function _year(s) { var m = String(s.title || "").match(/\((\d{4})\)/); return m ? parseInt(m[1], 10) : 0; }

// done({ attached, sourceId, blocked? }). Year is a DISAMBIGUATOR, not a hard gate:
// it only vetoes a candidate when BOTH sides carry a year and they clash (>±1).
// source titles usually carry NO year — a clean title match must attach.
function resolve(locgSeries, done) {
    var key = "map/" + locgSeries.id;
    var saved = store.get(key);
    if (saved) { done({ attached: true, sourceId: saved }); return; }
    if (_miss[key]) { done({ attached: false, sourceId: "" }); return; }
    searchFn(locgSeries.title, function(hits, meta) {
        if (meta && meta.blocked) { done({ attached: false, sourceId: "", blocked: true }); return; }
        var want = _norm(locgSeries.title);
        var wantYear = locgSeries.startYear || 0;
        var matches = (hits || []).filter(function(h) { return _norm(h.title) === want; });
        if (wantYear) {
            // year only vetoes when the candidate ALSO has one and it's far off
            matches = matches.filter(function(h) {
                var hy = _year(h);
                return !hy || Math.abs(hy - wantYear) <= 1;
            });
            if (matches.length > 1) {
                var exact = matches.filter(function(h) { return _year(h) === wantYear; });
                if (exact.length) matches = exact;
            }
        }
        if (matches.length === 1) {
            store.set(key, matches[0].id);
            done({ attached: true, sourceId: matches[0].id });
        } else {
            _miss[key] = true;          // session memory only — retry is free next launch
            done({ attached: false, sourceId: "" });
        }
    });
}
// A blocked source (cooldown) is neither persisted NOR marked in _miss — the blocked early-return
// happens BEFORE any store.set / _miss write, so a cooldown never poisons the mapping and a
// later un-blocked resolve of the same id re-searches. Retry is free.
