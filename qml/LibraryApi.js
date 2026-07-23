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

// ── buildRows — the one live snapshot the page renders ──
// Joins Collection entries with Progress (collapsed recent representatives), the manual
// watched mark (markFn), cached payload stamps (libNewCount/libAiring/libYear/libNotif),
// and the downloaded id set. Input-pure: the QML call site fetches everything and passes
// it in; the downloaded ids are derived there (CollectionBackfill mapping), never here.
//   progressList = Progress.recent("video", 0)   (one representative per series group)
//   markFn(id)   = Progress.watchedMark           (-1|0|1)
//   downloadedIds = array of Collection ids with >=1 episode on disk
function buildRows(entries, progressList, markFn, downloadedIds, nowMs) {
    entries = entries || [];
    progressList = progressList || [];
    var dl = _asSet(downloadedIds);
    var rows = [];
    for (var i = 0; i < entries.length; i++) {
        var e = entries[i];
        if (!e || !e.id) continue;
        var isSeries = (e.type === "series");
        var payload = e.payload || {};
        var pm = _matchProgress(String(e.id), progressList);
        var rawProgress = pm ? Number(pm.progress || 0) : 0;
        if (isNaN(rawProgress)) rawProgress = 0;
        var lastWatchedAt = pm ? Number(pm.updatedAt || 0) : 0;
        if (!lastWatchedAt) lastWatchedAt = Number(e.addedAt || 0);
        var mark = markFn ? markFn(e.id) : 0;
        // Ongoing series never auto-complete on episode %: feed the state calc an in-band
        // value so a caught-up show still reads "in progress" — raw progress stays for the bar.
        var stateProgress = (isSeries && rawProgress >= 0.90) ? 0.5 : rawProgress;
        var state = watchState(e, { progress: stateProgress, mark: mark, isSeries: isSeries });
        var notifOff = (payload.libNotif === false);
        var newCount = notifOff ? 0 : Math.max(0, Number(payload.libNewCount || 0));
        if (isNaN(newCount)) newCount = 0;
        rows.push({
            entry: e,
            state: state,
            progress: rawProgress,
            newCount: Math.min(newCount, 99),
            airing: String(payload.libAiring || ""),
            downloaded: dl[String(e.id)] === true,
            lastWatchedAt: lastWatchedAt,
            year: _year(payload),
            isSeries: isSeries
        });
    }
    return rows;
}

function _asSet(ids) {
    var s = {};
    ids = ids || [];
    for (var i = 0; i < ids.length; i++) s[String(ids[i])] = true;
    return s;
}

// A progress entry belongs to a Collection entry when its id equals the entry id (movie)
// or is that id followed by ":" (any episode of the series). The ":" boundary is robust to
// both id schemes in the tree (seriesRootId / seriesBaseId) and never cross-matches tt1/tt11.
function _matchProgress(entryId, progressList) {
    for (var i = 0; i < progressList.length; i++) {
        var p = progressList[i];
        var pid = String((p && p.id) || "");
        if (pid === entryId || pid.indexOf(entryId + ":") === 0) return p;
    }
    return null;
}

function _year(payload) {
    var y = Number(payload.libYear || payload.year);
    if (!isNaN(y) && y > 0) return y;
    var m = String(payload.releaseInfo || "").match(/\d{4}/);
    return m ? Number(m[0]) : 0;
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
