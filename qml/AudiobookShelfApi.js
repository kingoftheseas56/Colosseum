// Adapt the native AudiobookCatalog to Biblio's discovery and search surfaces.
.pragma library
.import "AudiobookFlowData.js" as FlowData

function isAvailable(catalog) {
    return !!catalog && typeof catalog.shelves === "function" && typeof catalog.shelf === "function"
}

function _list(v) { return (v && v.length !== undefined) ? Array.prototype.slice.call(v) : [] }

// A catalogue item -> the card shape PosterRail/CataloguePosterCard read. `raw` keeps the
// catalogue item so the tab can emit it untouched.
function card(item) {
    if (!item || typeof item !== "object") return null
    var out = { raw: item, ratingSource: "" }
    if (item.title) out.title = String(item.title)
    var authors = _list(item.authors).filter(function(a) { return !!a })
    if (authors.length > 0) out.author = authors.join(", ")
    if (item.coverUrl) out.cover = String(item.coverUrl)
    var id = item.recordingId || item.workId || item.id
    if (id) out.id = String(id)
    return out
}

function cards(items) {
    return _list(items).map(card).filter(function(c) { return c !== null })
}

function search(catalog, query, done) {
    if (!catalog || typeof catalog.search !== "function") {
        done([], qsTr("The audiobook catalogue is unavailable right now."))
        return
    }
    var limit = 24
    var args = [query, limit]
    var cancelled = false

    function cancel() {
        if (cancelled) return
        cancelled = true
        if (catalog.revisionChanged) catalog.revisionChanged.disconnect(read)
        if (catalog.requestFailed) catalog.requestFailed.disconnect(read)
    }

    function read() {
        if (cancelled) return
        var rows = _list(catalog.search(query, limit))
        var state = typeof catalog.remoteQueryState === "function" ? catalog.remoteQueryState("search", args) : null
        if (rows.length === 0 && state && state.pending && !state.cached) return
        cancel()
        if (rows.length === 0 && state && !state.cached) {
            if (state.status === "failure") {
                done([], qsTr("The audiobook catalogue search could not load."))
                return
            }
            if (state.enabled === false) {
                done([], qsTr("The audiobook catalogue is unavailable right now."))
                return
            }
        }

        // Local work lookups can supply covers and credits without starting a request per result.
        var localDetails = typeof catalog.ready === "function" && catalog.ready() && typeof catalog.work === "function"
        done(rows.filter(function(row) { return row && row.id }).map(function(row) {
            var work = localDetails ? (catalog.work(row.id) || {}) : row
            var recording = _list(work.recordings).find(function(edition) { return edition.coverUrl })
            return { workId: row.id, title: row.title, author: FlowData.authorFor(work),
                     cover: work.coverUrl || (recording && recording.coverUrl) || "" }
        }), "")
    }

    if (typeof catalog.retryRemoteQuery === "function") catalog.retryRemoteQuery("search", args)
    if (catalog.revisionChanged) catalog.revisionChanged.connect(read)
    if (catalog.requestFailed) catalog.requestFailed.connect(read)
    read()
    return cancelled ? undefined : cancel
}

function loadLanding(catalog) {
    var result = { available: isAvailable(catalog), rows: [], genres: [],
                   waiting: false, failed: false, retries: [] }
    if (result.available && typeof catalog.available === "function")
        result.available = catalog.available()
    if (!result.available) return result

    function track(method, args) {
        if (typeof catalog.remoteQueryState !== "function") return
        var state = catalog.remoteQueryState(method, args)
        if (!state || state.cached) return
        result.waiting = result.waiting || !!state.pending
        result.failed = result.failed || state.status === "failure" || state.enabled === false
        if (state.retryable) result.retries.push({ method: method, args: args })
    }

    var shelves = _list(catalog.shelves())
    track("shelves", [])
    for (var i = 0; i < shelves.length; i++) {
        var s = shelves[i]
        if (!s || !s.key) continue
        var key = String(s.key)
        var page = catalog.shelf(key, 0)
        track("shelf", [key, 0])
        var items = cards(page ? page.items : [])
        if (items.length === 0) continue
        result.rows.push({ key: key, title: s.title ? String(s.title) : "",
                           items: items, hasMore: !!(page && page.hasMore) })
    }
    result.genres = loadGenres(catalog)
    track("genres", [])
    return result
}

// [{ name, count?, cover? }] for GenreMosaic. covers: up to four urls per genre; the tile shows
// the first (the mosaic's single-image tile).
function genreTitle(key) {
    return String(key || "").replace(/-/g, " ").replace(/(^|\s)\S/g, function(character) {
        return character.toUpperCase()
    })
}

function loadGenres(catalog) {
    if (!catalog || typeof catalog.genres !== "function") return []
    return _list(catalog.genres()).filter(function(g) { return g && g.genre }).map(function(g) {
        var t = { name: genreTitle(g.genre), key: String(g.genre) }
        if (g.count !== undefined && g.count !== null) t.count = g.count
        var covers = _list(g.covers)
        if (covers.length > 0 && covers[0]) t.cover = String(covers[0])
        return t
    })
}

// Adapt the catalogue's page-number API to the existing see-all loader.
function loadPage(catalog, pin, page, done) {
    var method = pin.method
    var args = [pin.key, page]
    var cancelled = false

    function cancel() {
        if (cancelled) return
        cancelled = true
        if (catalog.revisionChanged) catalog.revisionChanged.disconnect(read)
        if (catalog.requestFailed) catalog.requestFailed.disconnect(read)
    }
    function read() {
        if (cancelled) return
        var result = catalog[method](pin.key, page)
        var personPage = method === "byPerson"
        var items = personPage ? _list(result) : _list(result && result.items)
        var state = typeof catalog.remoteQueryState === "function" ? catalog.remoteQueryState(method, args) : null
        var incomplete = personPage ? items.length === 0 : !result || result.items === undefined
        if (incomplete && (state ? state.pending : catalog.remotePending && catalog.remotePending())) return
        cancel()
        if (incomplete && state && state.status === "failure") {
            done({ error: qsTr("The audiobook catalogue could not load this page.") })
        } else {
            done({ items: cards(items), nextCursor: page + 1,
                   hasMore: personPage ? items.length === 24 : !!(result && result.hasMore) })
        }
    }

    if (!catalog) {
        done({ error: qsTr("The audiobook catalogue is unavailable right now.") })
        return
    }
    if (typeof catalog.retryRemoteQuery === "function") catalog.retryRemoteQuery(method, args)
    if (catalog.revisionChanged) catalog.revisionChanged.connect(read)
    if (catalog.requestFailed) catalog.requestFailed.connect(read)
    read()
    return cancelled ? undefined : cancel
}
