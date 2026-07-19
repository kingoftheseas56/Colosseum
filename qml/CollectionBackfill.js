.pragma library
// Maps download records → Collection entries for the one-time backfill of
// pre-existing downloads. Pure logic (no store access) so it's Node-testable.

// Strip the trailing ":season:episode" off a video stream id to get the base
// series id (tt123:1:5 -> tt123 ; mal:9:3:4 -> mal:9).
function seriesBaseId(streamId) {
    var parts = String(streamId || "").split(":")
    return parts.length > 2 ? parts.slice(0, parts.length - 2).join(":") : String(streamId || "")
}

// Theatre: LocalDownloads series record + its items. Movies key off id; episodes
// derive the base series id from a stream id. Cover = the record's art (real poster).
function entryForTheatreSeries(s, items) {
    if (!s) return null
    var title = s.title || ""
    var art = s.art || ""
    if (s.kind === "movie") {
        var mid = (items && items.length && items[0].id) ? String(items[0].id)
                  : String(s.key || "").replace(/^movie:/, "")
        if (!mid) return null
        return { "id": mid, "type": "movie", "title": title, "cover": art, "payload": { "art": art } }
    }
    if (!items || !items.length || !items[0].id) return null
    var sid = seriesBaseId(items[0].id)
    if (!sid) return null
    return { "id": sid, "type": "series", "title": title, "cover": art, "payload": { "art": art } }
}

// Tankoban: comic id is the prefixed seriesId (from the key); manga id is the title.
function entryForTankobanSeries(s) {
    if (!s) return null
    var title = s.title || ""
    var art = s.art || ""
    if (s.kind === "comic") {
        var cid = String(s.key || "").replace(/^comic:/, "")
        if (!cid) return null
        return { "id": cid, "type": "comic", "title": title, "cover": art, "payload": ({}) }
    }
    if (!title) return null   // manga reopens by title
    return { "id": title, "type": "manga", "title": title, "cover": art, "payload": ({}) }
}

// Biblio book/audiobook: pairKey computed by the caller (BiblioApi.pairKey / the
// audiobook record's own id). Cover empty -> gradient tile.
function entryForBook(rec, pairKey) {
    if (!rec || !pairKey) return null
    return { "id": String(pairKey), "type": "book",
             "title": rec.title || "", "cover": "",
             "payload": { "book": { "title": rec.title || "", "author": rec.author || "" } } }
}

// Normalized title key for cross-id dedup (a book saved as "title|author" and a
// backfill record's "title|" must not both land as separate tiles).
function titleKey(title) {
    return String(title || "").toLowerCase().replace(/\s+/g, " ").trim()
}

// Fill missing covers on Collection entries from a list of Progress entries (opened
// books carry a real cover there), matched by normalized title. Pure + reactive-friendly:
// callers pass Collection.items(world) and Progress.recent(kind). Returns a NEW array.
function withProgressCovers(entries, progressEntries) {
    var coverByTitle = {}
    for (var i = 0; i < (progressEntries || []).length; i++) {
        var p = progressEntries[i]
        if (p && p.cover) coverByTitle[titleKey(p.title)] = p.cover
    }
    var out = []
    for (var j = 0; j < (entries || []).length; j++) {
        var e = entries[j]
        if (e && (!e.cover || e.cover === "")) {
            var c = coverByTitle[titleKey(e.title)]
            if (c) {
                var e2 = {}
                for (var k in e) e2[k] = e[k]
                e2.cover = c
                e2.art = c
                out.push(e2)
                continue
            }
        }
        out.push(e)
    }
    return out
}
