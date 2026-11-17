// AudiobookShelfApi - the Biblio Audiobooks tab's one door to the catalogue.
// Calls the native AudiobookCatalog (shelves/shelf/genres/genre, see ROWS-CONTRACT.md) when it
// exists. With no catalogue it returns an empty result flagged unavailable; it never invents
// rows or items. Unknown item fields stay absent.
.pragma library

function isAvailable(catalog) {
    return !!catalog && typeof catalog.shelves === "function" && typeof catalog.shelf === "function"
}

function _list(v) { return (v && v.length !== undefined) ? Array.prototype.slice.call(v) : [] }

// A catalogue item -> the card shape PosterRail/CataloguePosterCard read. `raw` keeps the
// catalogue item so the tab can emit it untouched.
function card(item) {
    if (!item || typeof item !== "object") return null
    var out = { raw: item }
    if (item.title) out.title = String(item.title)
    var authors = _list(item.authors).filter(function(a) { return !!a })
    if (authors.length > 0) out.author = authors.join(", ")
    if (item.coverUrl) out.cover = String(item.coverUrl)
    var id = item.recordingId || item.workId
    if (id) out.id = String(id)
    return out
}

function cards(items) {
    return _list(items).map(card).filter(function(c) { return c !== null })
}

// { available, rows: [{ key, title, items: [card], hasMore }] } in catalogue order. A shelf with
// no items is dropped, so a row is never an empty header.
function loadRows(catalog) {
    if (!isAvailable(catalog)) return { available: false, rows: [] }
    var rows = []
    var shelves = _list(catalog.shelves())
    for (var i = 0; i < shelves.length; i++) {
        var s = shelves[i]
        if (!s || !s.key) continue
        var page = catalog.shelf(String(s.key), 0)
        var items = cards(page ? page.items : [])
        if (items.length === 0) continue
        rows.push({ key: String(s.key), title: s.title ? String(s.title) : "",
                    items: items, hasMore: !!(page && page.hasMore) })
    }
    return { available: true, rows: rows }
}

// [{ name, count?, cover? }] for GenreMosaic. covers: up to four urls per genre; the tile shows
// the first (the mosaic's single-image tile).
function loadGenres(catalog) {
    if (!catalog || typeof catalog.genres !== "function") return []
    return _list(catalog.genres()).filter(function(g) { return g && g.genre }).map(function(g) {
        var t = { name: String(g.genre) }
        if (g.count !== undefined && g.count !== null) t.count = g.count
        var covers = _list(g.covers)
        if (covers.length > 0 && covers[0]) t.cover = String(covers[0])
        return t
    })
}

// Downloaded audiobooks -> library cards with listening progress (0..1, absent when never played).
function libraryCards(downloads, progressStore) {
    return _list(downloads).filter(function(d) { return d && !d.missing }).map(function(d) {
        var key = d.recordingKey || d.id
        var c = { raw: d }
        if (d.title) c.title = String(d.title)
        if (d.author) c.author = String(d.author)
        if (key) c.id = String(key)
        var pg = (progressStore && key) ? progressStore.get("audiobook", String(key)) : null
        if (pg && pg.cover) c.cover = String(pg.cover)
        if (pg && pg.progress !== undefined && Number(pg.progress) > 0)
            c.progress = Math.max(0, Math.min(1, Number(pg.progress)))
        return c
    })
}
