.pragma library
.import "EpisodeBrowser.js" as EB
// NextUp.js — pure derivations for the "Next Up" home rows (spec 2026-07-18,
// Jellyfin library inheritance). The rule both worlds share: a series earns a
// card iff its MOST RECENT progress entry is finished (the ProgressStore 0.90
// watched line) — a half-finished latest entry belongs to Continue, never both.
// Everything here is fetch-free and QML-free so the headless harness proves it.

var WATCHED_LINE = 0.90;   // ProgressStore.h's line, same as EpisodeBrowser.rowState

function _finished(entry) {
    var e = entry || ({});
    return e.watched === true || Number(e.progress || 0) >= WATCHED_LINE;
}

// ---- Theatre ----

// Progress.recent("video") entries -> [{show, entry}] for shows whose latest
// episode entry is finished. recent() is recency-ordered, so the first entry
// seen per show IS its latest; movies (non-episode ids) never qualify.
function finishedShows(entries) {
    var seen = {}, out = [];
    var list = entries || [];
    for (var i = 0; i < list.length; i++) {
        var e = list[i] || ({});
        var id = String(e.id || "");
        if (!EB.isEpisodeId(id))
            continue;
        var show = EB.seriesRootId(id);
        if (seen[show])
            continue;
        seen[show] = true;
        if (_finished(e))
            out.push({ "show": show, "entry": e });
    }
    return out;
}

// The episode after nowId in the meta's ordering: same-season next number,
// else the FIRST episode of the next season (Specials/S0 skipped — Jellyfin
// rule). null when the show has nothing after nowId (caught up / ended).
function nextEpisodeFromMeta(videos, nowId) {
    var season = EB.seasonOf(nowId);
    if (season < 0)
        return null;
    var parts = String(nowId).split(":");
    var epNum = Number(parts[parts.length - 1]);
    if (isNaN(epNum))
        return null;
    var rows = EB.episodesFor(videos, season, EB.seriesRootId(nowId));
    for (var i = 0; i < rows.length; i++) {
        if (rows[i].num > epNum)
            return rows[i];                      // rows are ascending — first hit is next
    }
    var seasons = EB.seasonsFrom(videos);        // ascending, S0 pinned last
    for (var s = 0; s < seasons.length; s++) {
        if (seasons[s] === 0 || seasons[s] <= season)
            continue;
        var first = EB.episodesFor(videos, seasons[s], EB.seriesRootId(nowId));
        if (first.length)
            return first[0];
        // an empty listed season: keep walking (metadata gap, not "caught up")
    }
    return null;
}

// A finished show + its next episode -> a ContinueRow-shaped card. progress 0
// (the tile is fresh); resume carries what the click handlers need.
function theatreCard(finishedRec, next) {
    var e = finishedRec.entry || ({});
    var show = EB.showTitleFrom(e.title || e.caption || "");
    return {
        "id": next.id,
        "kind": "video",
        "nextUp": true,
        "title": show,
        "caption": show,
        "sub": "S" + next.season + " · E" + next.num
               + (next.title ? " · " + next.title : ""),
        "cover": e.cover || "",
        "c1": e.c1 || "#33445d", "c2": e.c2 || "#0c1118",
        "progress": 0,
        "resume": { "showId": finishedRec.show, "season": next.season,
                    "episode": next.num, "epTitle": next.title || "" }
    };
}

// ---- Tankoban manga (chapters + tankoban volumes; western comics EXCLUDED by ruling) ----

// Latest entry per series across the two manga kinds, finished only.
// Both lists are recency-ordered; merge keeps per-series first-seen = latest
// by comparing stamps when both kinds carry the same series (rare — a series
// reads in one mode at a time; first list wins on a tie).
function finishedReads(mangaEntries, tankobanEntries) {
    var seen = {}, out = [];
    var lists = [mangaEntries || [], tankobanEntries || []];
    for (var l = 0; l < lists.length; l++) {
        for (var i = 0; i < lists[l].length; i++) {
            var e = lists[l][i] || ({});
            var sid = String(e.id || "");
            if (!sid.length || seen[sid])
                continue;
            seen[sid] = true;
            if (_finished(e))
                out.push(e);
        }
    }
    return out;
}

// First float in a label: "Chapter 112.5" -> 112.5, "Vol. 3" -> 3. NaN when none.
function unitNumber(label) {
    var m = String(label || "").match(/(\d+(\.\d+)?)/);
    return m ? Number(m[1]) : NaN;
}

// The next unit STRICTLY after the finished one, from the units the world can
// actually open (downloaded chapters / ready volumes). units: [{id, label,
// number?}] in any order. null when the finished label has no number or
// nothing later exists on disk.
function nextUnit(finishedLabel, units) {
    var from = unitNumber(finishedLabel);
    if (isNaN(from))
        return null;
    var best = null, bestNum = NaN;
    var list = units || [];
    for (var i = 0; i < list.length; i++) {
        var u = list[i] || ({});
        var n = (u.number !== undefined && u.number !== null && String(u.number).length)
                ? Number(u.number) : unitNumber(u.label);
        if (isNaN(n) || n <= from)
            continue;
        if (best === null || n < bestNum) { best = u; bestNum = n; }
    }
    if (!best)
        return null;
    return { "id": String(best.id || ""), "label": String(best.label || ""), "number": bestNum };
}

// A finished read + its next unit -> a ContinueRow-shaped card. downloaded=false
// marks the dimmed "go download it" card (routes to the series page).
function mangaCard(entry, next, downloaded) {
    var e = entry || ({});
    return {
        "id": String(e.id || ""),
        "kind": String(e.kind || "manga"),
        "nextUp": true,
        "title": e.title || e.caption || "",
        "caption": e.title || e.caption || "",
        "sub": next.label + (downloaded ? "" : " · not downloaded"),
        "cover": e.cover || "",
        "c1": e.c1 || "#3a2f55", "c2": e.c2 || "#15111f",
        "progress": 0,
        "resume": { "unitId": next.id, "unitLabel": next.label,
                    "downloaded": downloaded === true }
    };
}
