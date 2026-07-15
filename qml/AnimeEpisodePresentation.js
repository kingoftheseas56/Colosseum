.pragma library

// AnimeEpisodePresentation.js — presentation-only selection for the anime
// Absolute/Seasons views (spec 2026-07-15). It NEVER parses a source dataset or
// derives a canonical number; it only chooses a mode, filters and orders rows
// the native AnimeOrderService already annotated, and builds playback targets
// that keep the original provider stream id, season, and episode. QML-free so
// the Node harness proves it.

// Row accessors tolerate both native-annotated rows (sourceSeason/sourceEpisode)
// and raw provider rows (season/episode), so non-anime models pass through.
function _season(e) {
    if (e.sourceSeason !== undefined) return Number(e.sourceSeason);
    if (e.season !== undefined) return Number(e.season);
    return Number(e.seasonNumber || 0);
}

function _episode(e) {
    if (e.sourceEpisode !== undefined) return Number(e.sourceEpisode);
    if (e.episode !== undefined) return Number(e.episode);
    return Number(e.number || 0);
}

// Absolute is only ever offered for a complete mapping; otherwise Seasons wins,
// even when Absolute was explicitly requested.
function effectiveMode(model, requested) {
    if (!model || model.absoluteComplete !== true) return "seasons";
    if (requested === "absolute" || requested === "seasons") return requested;
    return model.defaultOrder === "absolute" ? "absolute" : "seasons";
}

// Distinct season numbers, ascending, with Specials (season 0) pinned last.
function seasonNumbers(model) {
    var nums = [], seen = {};
    var seasons = (model && model.seasons) ? model.seasons : null;
    if (seasons && seasons.length) {
        for (var i = 0; i < seasons.length; i++) {
            var n = Number(seasons[i].number);
            if (!seen[n]) { seen[n] = true; nums.push(n); }
        }
    } else {
        var rows = (model && model.episodes) ? model.episodes : [];
        for (var j = 0; j < rows.length; j++) {
            var s = _season(rows[j]);
            if (!seen[s]) { seen[s] = true; nums.push(s); }
        }
    }
    nums.sort(function(a, b) {
        if (a === 0) return 1;
        if (b === 0) return -1;
        return a - b;
    });
    return nums;
}

// Rows shown for the current view. Absolute: mapped regular rows only, ordered
// by absolute number. Seasons: the active season's rows in source-episode order
// (season 0 shows the specials).
function visibleEpisodes(model, mode, activeSeason) {
    var rows = (model && model.episodes) ? model.episodes.slice() : [];
    if (effectiveMode(model, mode) === "absolute") {
        return rows.filter(function(e) { return e.kind === "episode" && e.mapped === true; })
                   .sort(function(a, b) { return a.absoluteNumber - b.absoluteNumber; });
    }
    return rows.filter(function(e) { return _season(e) === Number(activeSeason); })
               .sort(function(a, b) { return _episode(a) - _episode(b); });
}

// The autoplay queue rows. Same selection as visibleEpisodes; specials never
// enter the absolute queue.
function playbackEpisodes(model, mode, activeSeason) {
    return visibleEpisodes(model, mode, activeSeason);
}

// Playback targets in the shape the player's jumpToEpisode consumes, keeping the
// original provider stream id, season, and episode. Absolute titles read
// "Episode N" (continuous); the queue crosses provider season boundaries.
function playbackTargets(model, mode, activeSeason, showTitle, backdrop) {
    var eps = playbackEpisodes(model, mode, activeSeason);
    var absolute = effectiveMode(model, mode) === "absolute";
    var title = showTitle || "";
    var art = backdrop || "";
    var out = [];
    for (var i = 0; i < eps.length; i++) {
        var e = eps[i];
        // Absolute titles run continuously ("Episode N"); Seasons titles keep the
        // provider "SxEy" shape so the queue matches today's active-season queue.
        var label = (absolute && e.absoluteNumber !== undefined && e.absoluteNumber !== null)
                    ? ("Episode " + e.absoluteNumber)
                    : ("S" + _season(e) + "E" + _episode(e));
        out.push({
            "type": "series",
            "id": (e.streamId !== undefined ? e.streamId : e.id),
            "title": title + " - " + label,
            "backdrop": art,
            "season": _season(e),
            "episode": _episode(e),
            "metaLine": label
        });
    }
    return out;
}
