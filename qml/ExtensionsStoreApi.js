// ExtensionsStoreApi.js — the Store's catalogue: every community add-on listed on
// stremio-addons.net, fetched live (six pages of 100), cleaned, and sorted into the Store's rows.
// Rows are the ledger's (Brotherhood/agents/extensions-store-decision-ledger.md): Essentials, Most
// popular, then Play sources, Something to watch, Anime, World cinema, Subtitles, Better titles.
// The site is only the data source. Live TV, Music & Radio, adult and tracker-sync add-ons are out.
.pragma library

var API = "https://stremio-addons.net/api/v0/addons"
var PAGE = 100
var PER_ROW = 12

// The five a fresh Colosseum needs (Brotherhood/agents/extensions-store-decision-ledger.md).
var ESSENTIALS = ["OpenSubtitles v3", "Streaming Catalogs", "Comet | ElfHosted", "Anime Kitsu", "Torrentio"]

// [title, subtitle, test] in page order, over the site's own category slugs.
var ROWS = [
    ["Most popular", "The most starred add-ons on stremio-addons.net.", function(a) { return true }],
    ["Play sources", "Where your movies and shows actually stream from.", function(a) { return res(a, "stream") && isVideo(a) }],
    ["Something to watch", "New rows and catalogues for Theatre.", function(a) { return res(a, "catalog") && !res(a, "stream") }],
    ["Anime", "Seasons, catalogues and sources for anime.", function(a) { return focused(a, ["anime"]) }],
    ["World cinema", "Korean, Chinese, Japanese and Indian film and drama.", function(a) { return focused(a, ["asian drama", "bollywood"]) }],
    ["Subtitles", "Every language, matched to what is playing.", function(a) { return res(a, "subtitles") || has(a, "subtitles") }],
    ["Better titles", "Posters, ratings, episode orders and cleaner metadata.", function(a) { return has(a, "metadata") && !res(a, "stream") }]
]

// Focused: carries one of `cats`, and at most one other kind of content besides. Keeps the general
// sources (tagged with every category) out of the Anime and World cinema rows.
var KINDS = ["movies", "tv shows", "anime", "asian drama", "bollywood"]
function focused(a, cats) {
    var mine = 0, other = 0
    for (var i = 0; i < KINDS.length; i++) {
        if (!has(a, KINDS[i])) continue
        if (cats.indexOf(KINDS[i]) >= 0) mine++; else other++
    }
    return mine > 0 && other <= 1
}

function isVideo(a) {
    return a.types.indexOf("movie") >= 0 || a.types.indexOf("series") >= 0 || a.types.indexOf("anime") >= 0
}

var HIDDEN = ["live tv", "music", "radios"]

function has(a, cat) { return a.categories.indexOf(cat) >= 0 }

function firstSentence(s) {
    s = String(s || "").replace(/\s+/g, " ").trim()
    var m = s.match(/^.*?[.!?](\s|$)/)
    var out = m ? m[0].trim() : s
    return out.length > 140 ? out.slice(0, 137).trim() + "…" : out
}

function normalize(raw) {
    var m = raw.manifest || {}
    var res = (m.resources || []).map(function(r) { return typeof r === "string" ? r : (r && r.name) || "" })
    var hints = m.behaviorHints || {}
    return {
        id: String(m.id || raw.slug || ""),
        slug: String(raw.slug || ""),
        name: String(m.name || raw.slug || ""),
        title: String(m.name || raw.slug || "").replace(/\s*\|\s*ElfHosted$/i, ""),   // the host suffix is noise on a card
        // The app only loads https images; most http logos are served on https too.
        logo: String(m.logo || "").replace(/^http:\/\//, "https://"),
        description: firstSentence(m.description),
        stars: Number(raw.stars || 0),
        categories: (raw.categories || []).map(function(c) { return String(c.name || c.slug || "").toLowerCase() }),
        resources: res,
        types: (m.types || []).map(function(t) { return String(t) }),
        manifestUrl: String(raw.manifestUrl || ""),
        configureUrl: String(raw.configureUrl || ""),
        setupRequired: hints.configurationRequired === true,
        // Has its own setup page (Stremio's "Configure"). Comet, MediaFusion, AIOStreams et al.
        // only find streams once set up, so the Store opens that page instead of a bare install.
        configurable: hints.configurable === true || hints.configurationRequired === true,
        adult: hints.adult === true
    }
}

function visible(a, showExplicit) {
    if (!a.manifestUrl.length || !a.name.length) return false
    if (res(a, "addon_catalog")) return false
    if (/trakt|simkl|scrobbl/i.test(a.name)) return false          // tracker sync lives in Sync Center
    if (!showExplicit && (a.adult || has(a, "nsfw"))) return false
    // Deferred: Live TV, Music, Radio. A tagged add-on stays only if it is also a film source.
    for (var i = 0; i < HIDDEN.length; i++) if (has(a, HIDDEN[i]) && !has(a, "movies")) return false
    return true
}

function res(a, r) { return a.resources.indexOf(r) >= 0 }

// done(addons, error) — addons is the full cleaned list, most starred first.
function fetchAll(done) {
    var all = [], page = 1, finished = false
    function fail(msg) { if (!finished) { finished = true; done(all, msg) } }
    function next() {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            if (xhr.status !== 200) { fail("stremio-addons.net answered " + xhr.status); return }
            var body
            try { body = JSON.parse(xhr.responseText) } catch (e) { fail("stremio-addons.net sent something unreadable"); return }
            var list = body.addons || []
            for (var i = 0; i < list.length; i++) all.push(normalize(list[i]))
            var p = body.pagination || {}
            if (p.hasNextPage && page < 20) { page++; next(); return }
            finished = true
            all.sort(function(x, y) { return y.stars - x.stars })
            done(all, "")
        }
        xhr.open("GET", API + "?limit=" + PAGE + "&page=" + page)
        xhr.send()
    }
    next()
}

function essentials(all) {
    var out = []
    for (var i = 0; i < ESSENTIALS.length; i++)
        for (var j = 0; j < all.length; j++)
            if (all[j].name === ESSENTIALS[i]) { out.push(all[j]); break }
    return out
}

// [{ title, sub, items, all }] — each row's top add-ons by stars (all = the See-all list), Essentials left out.
function rows(all, showExplicit) {
    var shown = all.filter(function(a) { return visible(a, showExplicit) && ESSENTIALS.indexOf(a.name) < 0 })
    var out = []
    for (var i = 0; i < ROWS.length; i++) {
        var all = shown.filter(ROWS[i][2])
        if (all.length) out.push({ title: ROWS[i][0], sub: ROWS[i][1], items: all.slice(0, PER_ROW), all: all })
    }
    return out
}

// Hue from the name, so an add-on's card tint is stable between visits.
function hue(name) {
    var h = 0
    for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) % 360
    return h / 360
}
