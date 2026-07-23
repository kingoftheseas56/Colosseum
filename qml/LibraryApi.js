.pragma library

// LibraryApi — every pure derivation behind the Library page (spec §4). Fetch-free:
// ALL inputs are passed in (Progress/Collection/meta live in C++/QML, never here), so
// this whole module is provable headless. Proven by tests/library_api_harness.qml.

// watchState — the one truth-order: manual mark > movie-auto > episode progress.
// ctx = { progress: 0..1, mark: -1|0|1, isSeries: bool }. An ONGOING series is never
// auto-completed by episode %: buildRows feeds a series-aware progress here, but even
// raw a series at ≥0.90 with no mark reads "unwatched" (not "watched") by design.
function watchState(entry, ctx) {
    ctx = ctx || {};
    if (ctx.mark === 1) return "watched";
    if (ctx.mark === -1)
        return (ctx.progress > 0 && ctx.progress < 0.90) ? "progress" : "unwatched";
    if (!ctx.isSeries && ctx.progress >= 0.90) return "watched";
    if (ctx.progress > 0 && ctx.progress < 0.90) return "progress";
    return "unwatched";
}

// airingFrom — Ongoing/Ended from a Cinemeta-style meta (status first, releaseInfo fallback).
function airingFrom(meta) {
    var s = String((meta && meta.status) || "").toLowerCase();
    if (s.indexOf("end") === 0 || s === "completed") return "ended";
    if (s === "continuing" || s === "returning series" || s === "ongoing") return "ongoing";
    var ri = String((meta && meta.releaseInfo) || "");
    if (/^\d{4}\s*[-–]\s*\d{4}$/.test(ri)) return "ended";
    if (/^\d{4}\s*[-–]\s*$/.test(ri)) return "ongoing";
    return "";
}

// newEpisodeCount — episodes released after `sinceMs` and not in the future (<= nowMs). Cap 99.
function newEpisodeCount(videos, sinceMs, nowMs) {
    var n = 0;
    videos = videos || [];
    for (var i = 0; i < videos.length; i++) {
        var t = Date.parse(videos[i].released || "");
        if (!isNaN(t) && t > sinceMs && t <= nowMs) n++;
    }
    return Math.min(n, 99);
}

// episode order key: (season, episode|number) ascending; season 0 specials sort first.
function _epOrder(v) {
    var s = (v.season != null) ? Number(v.season) : 0;
    var e = (v.episode != null) ? Number(v.episode)
          : (v.number != null) ? Number(v.number) : 0;
    if (isNaN(s)) s = 0;
    if (isNaN(e)) e = 0;
    return s * 100000 + e;
}

// finaleWatched — is the LAST-ordered episode's id in the watched set? The caller gates
// this to ENDED series, so every episode has aired and the max (season,episode) IS the finale.
function finaleWatched(videos, watchedEpisodeIds) {
    videos = videos || [];
    if (!videos.length) return false;
    var finale = videos[0];
    var best = _epOrder(finale);
    for (var i = 1; i < videos.length; i++) {
        var k = _epOrder(videos[i]);
        if (k >= best) { best = k; finale = videos[i]; }
    }
    var ids = watchedEpisodeIds || [];
    for (var j = 0; j < ids.length; j++)
        if (ids[j] === finale.id) return true;
    return false;
}

// applyFilters — AND-compose the ledger state fragment, type, airing, and search needle.
function applyFilters(rows, state) {
    state = state || {};
    var sf = state.stateFilter || "";
    var tf = state.typeFilter || "";
    var af = state.airingFilter || "";
    var q = String(state.query || "").trim().toLowerCase();
    var out = [];
    for (var i = 0; i < (rows || []).length; i++) {
        var r = rows[i];
        if (sf === "inProgress" && r.state !== "progress") continue;
        if (sf === "unwatched" && r.state !== "unwatched") continue;
        if (sf === "watched" && r.state !== "watched") continue;
        if (sf === "newEpisodes" && !(r.newCount > 0)) continue;
        if (sf === "downloaded" && !r.downloaded) continue;
        if (tf === "movie" && r.isSeries) continue;
        if (tf === "series" && !r.isSeries) continue;
        if (af === "ongoing" && r.airing !== "ongoing") continue;
        if (af === "ended" && r.airing !== "ended") continue;
        if (q) {
            var title = String((r.entry && r.entry.title) || "").toLowerCase();
            if (title.indexOf(q) === -1) continue;
        }
        out.push(r);
    }
    return out;
}

// sortRows — returns a NEW ordered array; never mutates the input.
function sortRows(rows, mode) {
    var out = (rows || []).slice();
    function num(x) { return (typeof x === "number" && !isNaN(x)) ? x : 0; }
    if (mode === "added") {
        out.sort(function (a, b) { return num(b.entry && b.entry.addedAt) - num(a.entry && a.entry.addedAt); });
    } else if (mode === "az") {
        out.sort(function (a, b) {
            var ta = String((a.entry && a.entry.title) || "").toLowerCase();
            var tb = String((b.entry && b.entry.title) || "").toLowerCase();
            return ta < tb ? -1 : (ta > tb ? 1 : 0);
        });
    } else if (mode === "year") {
        out.sort(function (a, b) { return num(b.year) - num(a.year); });
    } else { // "lastWatched" (default)
        out.sort(function (a, b) { return num(b.lastWatchedAt) - num(a.lastWatchedAt); });
    }
    return out;
}

// ledgerCounts — the shelf ledger's six live numbers. "newEpisodes" counts SERIES that
// have new episodes (rows with newCount>0), matching the newEpisodes filter — not the sum.
function ledgerCounts(rows) {
    rows = rows || [];
    var c = { saved: rows.length, inProgress: 0, unwatched: 0, watched: 0, newEpisodes: 0, downloaded: 0 };
    for (var i = 0; i < rows.length; i++) {
        var r = rows[i];
        if (r.state === "progress") c.inProgress++;
        else if (r.state === "unwatched") c.unwatched++;
        else if (r.state === "watched") c.watched++;
        if (r.newCount > 0) c.newEpisodes++;
        if (r.downloaded) c.downloaded++;
    }
    return c;
}
