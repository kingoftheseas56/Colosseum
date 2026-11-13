.pragma library

// Ephemeral presentation projection. VaultLibrary and Progress remain the owners.
function copy(value) { var out = {}; for (var k in value) out[k] = value[k]; return out }
function fraction(value) { return Math.max(0, Math.min(1, Number(value) || 0)) }
function normalize(group, items, structure, hidden, detail) {
    detail = detail || {}
    items = items || []
    var first = items[0] || {}, identityId = String(group.identityId || "")
    var adoptedTitle = String(group.identityTitle || "").trim()
    for (var a = 0; a < items.length; ++a) {
        var candidate = items[a]
        if (candidate.identitySuppressed || !String(candidate.identityTitle || "").trim()) continue
        if (identityId && candidate.identityId && candidate.identityId !== identityId) continue
        if (!identityId) identityId = String(candidate.identityId || "")
        if (identityId) { first = candidate; adoptedTitle = String(candidate.identityTitle).trim(); break }
    }
    // Native series.title and item.title both fall back to filenames. EPUB metadataSource
    // is set even when the embedded title is empty, so it is not proof of a curated title.
    var identified = !!identityId && !!adoptedTitle
    var addedAt = 0
    for (var m = 0; m < items.length; ++m) addedAt = Math.max(addedAt, Number(items[m].mtimeMs) || 0)
    var seasons = (structure || {}).seasons || [], show = false
    for (var s = 0; s < seasons.length; ++s)
        if (seasons[s].key && !seasons[s].supplemental) show = true
    var kind = group.kind === "video" ? (show ? "show" : "movie") : group.kind
    var title = identified ? adoptedTitle : "Needs a match"
    var cover = group.coverUrl || first.coverUrl || ""
    var path = first.path || "", key = String(group.key || group.subtreePath || first.id || "")
    var offline = items.length > 0
    for (var i = 0; i < items.length; ++i) if (!items[i].away) offline = false
    if (!items.length) offline = Number(group.awayCount) > 0
    var progressItems = items
    if (kind === "movie" && detail.copies) {
        var mainPaths = {}
        for (var cp = 0; cp < detail.copies.length; ++cp) mainPaths[detail.copies[cp].path] = true
        progressItems = items.filter(function(file) { return !!mainPaths[file.path] })
    }
    var identity = { world: group.identityWorld || (group.kind === "video" ? "Theatre" : group.kind === "book" ? "Biblio" : "Tankoban"),
        identityId: identityId, source: group.identSource || first.identitySource || "", title: title,
        coverUrl: cover, type: show ? "series" : "movie", synopsis: group.synopsis || first.synopsis || "" }
    return { id: identityId || first.id || key, seriesKey: key, key: key, kind: kind, nativeKind: group.kind,
        title: title, matchTitle: group.title || first.displayTitle || "", author: first.author || "",
        year: group.identityYear || first.identityYear || detail.year || 0, subtitle: first.author || "",
        poster: cover, cover: cover, coverUrl: cover, backdrop: "", logo: "", synopsis: identity.synopsis,
        blurb: identity.synopsis, runtimeText: detail.runtimeText || runtime(first.durationSec),
        genres: detail.genresLine || "", imdbRating: /^IMDb ([0-9]+(?:\.[0-9]+)?)$/.test(detail.ratingText || "") ? Number(String(detail.ratingText).slice(5)) : 0,
        progress: 0, progressFraction: 0, progressText: "", addedAt: addedAt, isNew: addedAt > 0 && Date.now() - addedAt >= 0 && Date.now() - addedAt < 7 * 86400000, completed: false, lastReadMs: 0, finished: false, hidden: !!hidden,
        offline: offline, identified: identified, payload: group, items: items, progressItems: progressItems, copies: [group],
        identity: identity, path: path, route: group.kind === "video" ? "theatre" : group.kind,
        seasons: seasons, seasonCount: seasons.filter(function(s) { return !!s.key && !s.supplemental }).length,
        count: Number(group.count) || items.length, serverId: "" }
}
function runtime(seconds) {
    var minutes = Math.floor(Number(seconds) / 60)
    if (!isFinite(minutes) || minutes < 1) return ""
    return minutes >= 60 ? Math.floor(minutes / 60) + "h" + (minutes % 60 ? " " + (minutes % 60) + "m" : "") : minutes + "m"
}
function episodeFor(row, file, record) {
    var seasons = row.seasons || []
    for (var s = 0; s < seasons.length; ++s) {
        if (seasons[s].supplemental) continue
        var episodes = seasons[s].episodes || []
        for (var e = 0; e < episodes.length; ++e)
            if (episodes[e].id === file.id && Number(episodes[e].episode) > 0)
                return {season: Number(episodes[e].season), episode: Number(episodes[e].episode)}
    }
    var match = /^tt\d+:(\d+):(\d+)$/.exec(String(record.id || ""))
    if (row.serverId && match) return {season:Number(match[1]), episode:Number(match[2])}
    return null
}
function progressLabel(row, file, record) {
    var resume = record.resume || {}, parts = [], percent = Math.round(fraction(record.progress) * 100) + "%"
    if (row.nativeKind === "video") {
        var ep = episodeFor(row, file, record)
        if (ep) parts.push("S" + ep.season + " E" + ep.episode)
        var duration = Number(record.duration) || Number(file.durationSec)
        var position = Number(resume.position)
        if (duration > 0 && isFinite(position) && position >= 0 && position < duration)
            parts.push(Math.ceil((duration - position) / 60) + " min left")
        else parts.push(percent)
    } else if (row.nativeKind === "comic" && Number(resume.page) > 0) {
        // ComicReaderShell uses one-based page numbers.
        parts.push("Page " + Number(resume.page))
        if (Number(file.pages) > 0) parts[0] += " of " + Number(file.pages)
    } else {
        // Reader2's unified book Progress has no chapter location, only percent/path.
        if (resume.chapterNumber !== undefined && String(resume.chapterNumber).length)
            parts.push("Chapter " + resume.chapterNumber)
        parts.push(percent)
    }
    return parts.join(" · ")
}
function deduplicate(rows) {
    var out = [], byId = {}
    for (var i = 0; i < rows.length; ++i) {
        var r = rows[i], key = r.kind + ":" + (r.identity.identityId || r.seriesKey)
        if (!byId[key]) { byId[key] = r; out.push(r); continue }
        var previous = byId[key]
        previous.copies = previous.copies.concat(r.copies)
        previous.items = previous.items.concat(r.items)
        previous.progressItems = previous.progressItems.concat(r.progressItems)
        previous.seasons = previous.seasons.concat(r.seasons)
        var seasonNumbers = {}
        previous.seasons.forEach(function(season) {
            if (season.key && !season.supplemental) seasonNumbers[season.season !== undefined ? season.season : season.key] = true
        })
        previous.seasonCount = Object.keys(seasonNumbers).length
        previous.addedAt = Math.max(previous.addedAt, r.addedAt)
        previous.isNew = previous.isNew || r.isNew
        if (!previous.blurb) { previous.blurb = r.blurb; previous.synopsis = r.synopsis }
        if (!previous.runtimeText) previous.runtimeText = r.runtimeText
        if (!previous.genres) previous.genres = r.genres
        if (!previous.imdbRating) previous.imdbRating = r.imdbRating
        if (previous.offline && !r.offline) previous.path = r.path
        previous.offline = previous.offline && r.offline
        if (!previous.cover) { previous.cover = r.cover; previous.poster = r.poster; previous.coverUrl = r.coverUrl }
    }
    return out
}
function joinProgress(row, progress, admission) {
    var out = copy(row), best = null, target = null, completed = {}, completionKeys = {}
    if (progress && typeof progress.get === "function") {
        var candidates = row.serverId ? [{id: row.id, kind: "video"}] : row.progressItems || row.items
        for (var i = 0; i < candidates.length; ++i) {
            var file = candidates[i]
            if (row.kind === "show" && !row.serverId && !episodeFor(row, file, {})) continue
            var pg = progress.get(file.kind || row.nativeKind, file.id)
            var mark = row.nativeKind === "video" && typeof progress.watchedMark === "function" ? progress.watchedMark(file.id) : 0
            var done = mark === 1 || (mark !== -1 && !!(pg && (pg.finished || pg.watched || fraction(pg.progress) === 1 || (pg.resume && pg.resume.finished))))
            var ep = episodeFor(row, file, pg || {})
            var completionKey = ep ? ep.season + ":" + ep.episode : file.id
            if (row.kind !== "show" || ep) { completionKeys[completionKey] = true; if (done) completed[completionKey] = true }
            if (!row.serverId && row.nativeKind === "video" && (!admission || admission[file.id] !== "Admitted")) continue
            if (pg && pg._homeServerHidden) continue
            if (row.serverId && String(((pg || {}).resume || {}).infoHash || "").indexOf("home-server:" + row.serverId + ":") !== 0) continue
            if (pg && pg.id && (!best || Number(pg.updatedAt) > Number(best.updatedAt))) { best = pg; target = file; out.finished = done }
        }
    }
    // Server episode records use canonical title:season:episode ids and are already
    // persisted by HomeServerProgressBridge into the same Progress owner.
    if (row.serverId && progress && typeof progress.recent === "function") {
        var recent = progress.recent("video", 0)
        for (var j = 0; j < recent.length; ++j) {
            var entry = recent[j], resume = entry.resume || {}
            if (entry._homeServerHidden) continue
            if (String(resume.infoHash || "").indexOf("home-server:" + row.serverId + ":") !== 0) continue
            if (entry.id !== row.id && !(row.kind === "show" && String(entry.id).indexOf(row.id + ":") === 0)) continue
            if (!best || Number(entry.updatedAt) > Number(best.updatedAt)) { best = entry; target = {}; out.finished = !!(entry.finished || entry.watched || fraction(entry.progress) === 1 || resume.finished) }
        }
    }
    if (best) {
        out.progress = fraction(best.progress); out.progressFraction = out.progress
        out.lastReadMs = Number(best.updatedAt) || 0
        out.progressText = progressLabel(row, target, best)
        out.progressRecord = best
        out.resumePath = target.path || ""
        out.resumeOffline = !!target.away
        out.hasProgress = true
    }
    var keys = Object.keys(completionKeys), doneKeys = Object.keys(completed)
    out.completed = row.kind === "show" || row.kind === "comic" ? keys.length > 0 && keys.length === doneKeys.length : doneKeys.length > 0
    if (row.serverId && row.kind === "movie") out.completed = out.finished
    return out
}
function continuing(rows) {
    return rows.filter(function(r) { return r.identified && !r.hidden && r.hasProgress && r.progress > 0 && r.progress < 1 && !r.finished })
        .sort(function(a,b) { return b.lastReadMs - a.lastReadMs })
}
function search(rows, query) {
    var q = String(query || "").trim().toLowerCase()
    return rows.filter(function(r) {
        var text = [r.title, r.author, r.year, r.matchTitle]
        for (var i = 0; i < r.items.length; ++i) text.push(r.items[i].realName || r.items[i].path || "")
        return !q || text.join(" ").toLowerCase().indexOf(q) >= 0
    })
}
function serverRow(row, server) {
    var out = normalize({key: row.id, kind: "video", identityId: row.id, identityTitle: row.title, title: row.title,
        coverUrl: row.coverUrl, identityYear: row.year, identityWorld: "Theatre"}, [], {}, false)
    out.kind = row.kind === "series" ? "show" : "movie"
    out.identity = row.identity || out.identity
    out.payload = row; out.serverId = server.id; out.offline = !!server.offline || !!server.needsReconnect || server.enabled === false
    out.profileGeneration = server.profileGeneration
    return out
}
