// ComicResolve.js — the attach machine: pairs a LOCG catalogue series with the xoxo slug
// that actually serves its pages. Resolved ONCE per series, persisted forever. Conservative:
// an ambiguous or year-mismatched candidate is a NO-match — a wrong comic must never open
// silently. store + xoxoSearchFn are INJECTED (Main.qml wires QSettings + Xoxo.searchSeries;
// tests inject fakes) — pure/testable, the XoxoApi nowFn lesson.
.pragma library

var store = null;          // injected: { get(key)->string, set(key, value) } — QSettings-backed
var xoxoSearchFn = null;   // injected: Xoxo.searchSeries(query, cb(hits, meta))

function _norm(t) {
    return String(t).toLowerCase()
        .replace(/\(\d{4}\)/g, "")            // strip the year suffix
        .replace(/[^a-z0-9]+/g, " ")           // punctuation -> space
        .replace(/\s+/g, " ").trim();
}
function _year(s) { var m = String(s.title || "").match(/\((\d{4})\)/); return m ? parseInt(m[1], 10) : 0; }

// done({ attached: bool, xoxoId: string })
function resolve(locgSeries, done) {
    var key = "map/" + locgSeries.id;
    var saved = store.get(key);
    if (saved) { done({ attached: true, xoxoId: saved }); return; }
    if (store.get(key + "/none")) { done({ attached: false, xoxoId: "" }); return; }
    xoxoSearchFn(locgSeries.title, function(hits, meta) {
        if (meta && meta.blocked) { done({ attached: false, xoxoId: "", blocked: true }); return; }
        var want = _norm(locgSeries.title);
        var wantYear = locgSeries.startYear || 0;
        var candidates = (hits || []).filter(function(h) {
            if (_norm(h.title) !== want) return false;
            var hy = _year(h);
            if (wantYear && hy) return Math.abs(hy - wantYear) <= 1;   // year gate +/-1
            return !wantYear && !hy;   // neither side has a year -> title-only match allowed
        });
        if (wantYear) {
            var exact = candidates.filter(function(h) { return _year(h) === wantYear; });
            if (exact.length) candidates = exact;
        }
        if (candidates.length === 1) {
            store.set(key, candidates[0].id);
            done({ attached: true, xoxoId: candidates[0].id });
        } else {
            store.set(key + "/none", "1");   // remembered so we don't re-search every open
            done({ attached: false, xoxoId: "" });
        }
    });
}
// A blocked xoxo (cooldown) is NOT persisted as no-match — the blocked early-return happens
// BEFORE any store.set, so a cooldown never poisons the permanent mapping. Retry is free.
