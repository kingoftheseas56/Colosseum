.pragma library
// EpisodeBrowser.js — pure derivations for the in-player episode/source drawer (Feature 8).
// Everything here is mpv-free and QML-free so the headless harness can prove it behaves.
// Season/episode rules mirror TheatreSeries.qml (computeSeasons/filterEpisodes) and
// ProgressStore.h (seriesRootId, the 0.90 watched line) — origins named per function.

// ProgressStore.h seriesRootId, in JS: tt ids root to the tt part; provider ids
// (kitsu:123:2:5) keep provider:id.
function seriesRootId(id) {
    var s = String(id || "");
    if (!s.length)
        return "";
    var parts = s.split(":");
    if (parts.length < 3)
        return s;
    if (s.indexOf("tt") === 0)
        return parts[0];
    return parts[0] + ":" + parts[1];
}

// The season slot inside an episode id ("tt1:3:4" -> 3, "kitsu:7442:2:5" -> 2), -1 if none.
function seasonOf(id) {
    var s = String(id || "");
    var parts = s.split(":");
    if (parts.length < 3)
        return -1;
    var slot = (s.indexOf("tt") === 0) ? parts[1] : parts[parts.length - 2];
    var n = Number(slot);
    return isNaN(n) ? -1 : n;
}

// Queue titles arrive as "Show - SxEy" (TheatreSeries.shallowEpisodeTarget); the drawer
// needs the bare show name for building new queue titles.
function showTitleFrom(mediaTitle) {
    return String(mediaTitle || "").replace(/ - S\d+E\d+$/, "");
}

function _season(v) { return (v.season !== undefined) ? v.season : (v.seasonNumber || 0); }
function _episode(v) { return (v.episode !== undefined) ? v.episode : (v.number || 0); }

// TheatreSeries.computeSeasons: distinct, ascending, Specials (S0) pinned last.
function seasonsFrom(videos) {
    var seen = {}, out = [];
    var vids = videos || [];
    for (var i = 0; i < vids.length; i++) {
        var s = _season(vids[i]);
        if (s >= 0 && !seen[s]) { seen[s] = true; out.push(s); }
    }
    out.sort(function(a, b) {
        if (a === 0) return 1;
        if (b === 0) return -1;
        return a - b;
    });
    return out;
}

// One season's rows, episode-ordered. Missing video ids are built the way
// TheatreSeries.episodeStreamId builds them: series:season:episode.
function episodesFor(videos, season, seriesId) {
    var out = [];
    var vids = videos || [];
    for (var i = 0; i < vids.length; i++) {
        var v = vids[i];
        if (_season(v) !== season)
            continue;
        out.push({
            "id": (v.id && String(v.id).length) ? String(v.id)
                  : (seriesId + ":" + _season(v) + ":" + _episode(v)),
            "num": _episode(v),
            "season": _season(v),
            "title": v.name || v.title || ("Episode " + _episode(v))
        });
    }
    out.sort(function(a, b) { return a.num - b.num; });
    for (var k = 0; k < out.length; k++)
        out[k].queueIdx = k;
    return out;
}

// Rows -> playable queue targets, the shallowEpisodeTarget shape jumpToEpisode expects.
function queueFrom(rows, showTitle, backdrop) {
    var out = [];
    var rs = rows || [];
    for (var i = 0; i < rs.length; i++) {
        var r = rs[i];
        out.push({
            "type": "series",
            "id": r.id,
            "title": showTitle + " - S" + r.season + "E" + r.num,
            "backdrop": backdrop || "",
            "season": r.season,
            "episode": r.num
        });
    }
    return out;
}

// The traveling episodeQueue rendered as rows with zero fetch (the instant floor).
function floorRows(queue) {
    var out = [];
    var q = queue || [];
    for (var i = 0; i < q.length; i++) {
        var e = q[i] || ({});
        out.push({
            "id": String(e.id || ""),
            "num": (e.episode !== undefined) ? e.episode : 0,
            "season": (e.season !== undefined) ? e.season : 0,
            "title": String(e.title || ""),
            "queueIdx": i
        });
    }
    return out;
}

// Episode row visual state from its ProgressStore record. The 0.90 watched line and the
// explicit `watched` flag both mirror ProgressStore.h. The playing row is always "now".
function rowState(progressRecord, rowId, nowId) {
    var rec = progressRecord || ({});
    var frac = Number(rec.progress || 0);
    if (rowId && rowId === nowId)
        return { "state": "now", "frac": frac };
    if (rec.watched === true || frac >= 0.90)
        return { "state": "watched", "frac": 1 };
    if (frac > 0)
        return { "state": "inProgress", "frac": frac };
    return { "state": "unwatched", "frac": 0 };
}

// Source row: the playing candidate, a candidate that already died this session, or playable.
function sourceRowState(index, currentIndex, isDead) {
    if (index === currentIndex)
        return "now";
    if (isDead)
        return "dead";
    return "playable";
}
