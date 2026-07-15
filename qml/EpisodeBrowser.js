.pragma library
.import "AnimeEpisodePresentation.js" as AnimeEpisodePresentation
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

// ---- context hydration (bare-door fix 2026-07-12) ----
// Continue-Watching resume and downloaded files open the player with no episode queue
// and no source list, so prev/next-episode and change-stream buttons vanish. These pure
// helpers let the player rebuild both from the identities it always has.

// Only ids with numeric season+episode slots hydrate. Routing prefixes that merely
// LOOK multi-part (iptv:/url:/local:) never do.
function isEpisodeId(id) {
    var s = String(id || "");
    if (/^(iptv|url|local):/.test(s))
        return false;
    var parts = s.split(":");
    if (parts.length < 3)
        return false;
    var season = parts[parts.length - 2], episode = parts[parts.length - 1];
    return /^\d+$/.test(season) && /^\d+$/.test(episode);
}

// Cinemeta meta.videos -> the playing episode's season queue, in the exact
// {episodeQueue, episodeIndex, year} shape PlayerPage.resolveAdjacentContext eats.
// null when the episode isn't in the meta — an honest no-op, never a guessed queue.
function queueContextFromMeta(videos, nowId, showTitle, backdrop, year) {
    if (!isEpisodeId(nowId))
        return null;
    var season = seasonOf(nowId);
    if (season < 0)
        return null;
    var rows = episodesFor(videos, season, seriesRootId(nowId));
    var queue = queueFrom(rows, showTitle, backdrop);
    for (var i = 0; i < queue.length; i++) {
        if (queue[i].id === String(nowId))
            return { "episodeQueue": queue, "episodeIndex": i, "year": String(year || "") };
    }
    return null;
}

// Anime absolute queue (spec 2026-07-15): when the native order model is a
// COMPLETE mapping, the bare door recovers one continuous cross-season queue
// keyed on the original provider stream ids. Returns null for an incomplete or
// unavailable model so the caller falls back to queueContextFromMeta. null too
// when the playing id isn't in the queue — an honest no-op, never a guess.
function queueContextFromOrder(model, nowId, showTitle, backdrop, year) {
    if (!model || model.absoluteComplete !== true)
        return null;
    var queue = AnimeEpisodePresentation.playbackTargets(model, "absolute", 0, showTitle, backdrop);
    for (var i = 0; i < queue.length; i++) {
        if (String(queue[i].id) === String(nowId))
            return { "episodeQueue": queue, "episodeIndex": i, "year": String(year || "") };
    }
    return null;
}

// The playing candidate's identity, PlayerPage.streamCandidateKey style; direct-url
// candidates ride the "url:<url>" hash prefix, so a bare url maps onto the same key.
function _candidateKey(c) {
    var cand = c || {};
    var hash = String(cand.infoHash || "");
    if (!hash.length && cand.url && String(cand.url).length)
        hash = "url:" + String(cand.url);
    if (!hash.length)
        return "";
    return hash.toLowerCase() + ":" + Number(cand.fileIdx || 0);
}

// Freshly fetched source rows merged around the one already playing: found -> keep its
// slot, unknown -> prepend it (never lose the live stream). null when there's nothing
// to merge (empty fetch) or no safe identity to merge on.
function mergeHydratedCandidates(current, fetched) {
    var rows = fetched || [];
    if (!rows.length)
        return null;
    var key = _candidateKey(current);
    if (!key.length)
        return null;
    for (var i = 0; i < rows.length; i++) {
        if (_candidateKey(rows[i]) === key)
            return { "list": rows, "index": i };
    }
    return { "list": [current].concat(rows), "index": 0 };
}

// Torrent continuity (spec 2026-07-11): an episode jump should stay on the torrent
// already playing when it also carries the target episode (season packs). Given the
// freshly resolved rows and the currently playing hash/file, return the row to play:
// a row with the SAME infoHash but a DIFFERENT fileIdx wins regardless of rank.
// "url:"-prefixed hashes never match (same url = same video, wrong for another
// episode); same hash + same fileIdx is the same file, also wrong. No match (or no
// current hash) -> rows[0], exactly the pre-feature behavior. null on empty input.
function pickContinuityRow(rows, currentInfoHash, currentFileIdx) {
    if (!rows || !rows.length)
        return null;
    var cur = String(currentInfoHash || "").toLowerCase();
    if (cur.length && cur.indexOf("url:") !== 0) {
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i] || {};
            var h = String(r.infoHash || "").toLowerCase();
            if (h === cur && Number(r.fileIdx || 0) !== Number(currentFileIdx || 0))
                return r;
        }
    }
    return rows[0];
}
