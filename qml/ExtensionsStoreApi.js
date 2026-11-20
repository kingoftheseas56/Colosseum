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
var ESSENTIALS = ["Torrentio", "OpenSubtitles v3", "Streaming Catalogs", "Comet | ElfHosted", "Anime Kitsu"]

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

// Torrentio by stable identity (WT-05), never by display name.
function isTorrentio(a) {
    return a.id === "com.stremio.torrentio.addon"
        || a.manifestUrl === "https://torrentio.strem.fun/manifest.json"
}

function essentialName(a) { return isTorrentio(a) ? "Torrentio" : a.name }

// Keep the bundled Theatre source discoverable independently of the community index
// (fresh, offline or failed catalogue). This only supplies a Store card; installation and
// enablement stay explicit user actions through the existing native manager. A valid remote
// Torrentio entry keeps its own metadata/configuration URL.
function withTorrentio(all) {
    var out = all.filter(function(a) { return !isTorrentio(a) || visible(a, false) })
    for (var i = 0; i < out.length; i++)
        if (isTorrentio(out[i])) return out
    out.push(normalize({
        slug: "torrentio", manifestUrl: "https://torrentio.strem.fun/manifest.json",
        configureUrl: "https://torrentio.strem.fun/configure",
        categories: [{name: "movies"}, {name: "tv shows"}, {name: "anime"}],
        manifest: {id: "com.stremio.torrentio.addon", name: "Torrentio",
            logo: "../assets/addon-logos/torrentio.png",
            description: "Provides torrent streams from scraped torrent providers.",
            resources: ["stream"], types: ["movie", "series", "anime"],
            behaviorHints: {configurable: true}}
    }))
    return out
}

// done(addons, error) — addons is the full cleaned list, most starred first.
function fetchAll(done) {
    var all = [], page = 1, finished = false
    function fail(msg) { if (!finished) { finished = true; done(withTorrentio(all), msg) } }
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
            done(withTorrentio(all), "")
        }
        xhr.open("GET", API + "?limit=" + PAGE + "&page=" + page)
        xhr.send()
    }
    next()
}

// Keep native sources and the first-run stream and subtitle choices available offline.
function essentialsPreview(all) {
    return [
        Object.assign({}, withTorrentio(all).find(isTorrentio), { description: "Movie, show and anime streams" }),
        bundled("colosseum.well.tankoyomi", "tankoyomi", "Tankoyomi", "tankoyomi.png",
                "https://colosseum-tankoyomi.colosseum-watchparty-relay.workers.dev/manifest.json",
                "Manga chapter sources", true),
        bundled("org.stremio.opensubtitlesv3", "opensubtitles", "OpenSubtitles v3",
                "opensubtitles.png", "https://opensubtitles-v3.strem.io/manifest.json", "Subtitles"),
        bundled("colosseum.well.nyaa", "nyaa", "Nyaa", "nyaa.png",
                "colosseum://well/nyaa", "Manga volume sources"),
        bundled("colosseum.well.getcomics.issues", "getcomics", "GetComics", "getcomics.png",
                "colosseum://well/getcomics.issues", "Comic issue sources"),
        bundled("colosseum.well.indexers", "tankorent", "Tankorent", "tankorent.png",
                "https://colosseum-tankorent.colosseum-watchparty-relay.workers.dev/manifest.json",
                "Comic and book torrent sources", true),
        bundled("colosseum.well.libgen", "libgen", "LibGen", "libgen.ico",
                "colosseum://well/libgen", "Book sources"),
        bundled("colosseum.well.audiobookbay", "audiobookbay", "AudioBookBay", "audiobookbay.png",
                "colosseum://well/audiobookbay", "Audiobook sources")
    ]
}

function bundled(id, slug, name, art, url, description, configurable) {
    return {
        id: id, slug: slug, name: name, title: name, manifestUrl: url,
        description: description, categories: [], stars: 0,
        logo: art ? "../assets/addon-logos/" + art : "",
        configurable: !!configurable
    }
}

// The directory can be delayed or unavailable. Keep these nine known entries
// actionable while it loads, then prefer the matching directory record.
var MORE_SOURCES = [
    ["comet", "comet.elfhosted.com.aKyc", "Comet", "comet.png",
     "https://comet.elfhosted.com/manifest.json", "https://comet.elfhosted.com/configure", false,
     "Movie, show and anime streams"],
    ["mediafusion-elfhosted", "stremio.addons.mediafusion|elfhosted", "MediaFusion", "mediafusion.png",
     "https://mediafusion.elfhosted.com/manifest.json", "https://mediafusion.elfhosted.com/configure", false,
     "Torrent and HTTP streams"],
    ["aiostreams", "com.aiostreams.viren070", "AIOStreams", "aiostreams.png",
     "https://aiostreams.elfhosted.com/stremio/manifest.json", "https://aiostreams.elfhosted.com/stremio/configure", true,
     "Combine and filter stream sources"],
    ["subsense", "com.subsense.nepiraw", "SubSense", "",
     "https://subsense.nepiraw.com/manifest.json", "https://subsense.nepiraw.com/configure", true,
     "Subtitles from multiple providers"],
    ["subdl-subtitles", "community.subdl.subtitles", "SubDL", "subdl.jpg",
     "https://subdl.strem.top/manifest.json", "https://subdl.strem.top/configure", true,
     "Subtitles from SubDL"]
]

var ENRICHMENT = [
    ["streaming-catalogs", "pw.ers.netflix-catalog", "Streaming Catalogs", "streaming-catalogs.png",
     "https://7a82163c306e-stremio-netflix-catalog-addon.baby-beamup.club/manifest.json",
     "https://7a82163c306e-stremio-netflix-catalog-addon.baby-beamup.club/configure", false,
     "Movie and show catalogues by service"],
    ["aiometadata-elfhosted", "com.aio.metadata", "AIOMetadata", "",
     "https://aiometadata.elfhosted.com/stremio/manifest.json", "https://aiometadata.elfhosted.com/stremio/configure", true,
     "Choose metadata and artwork providers"],
    ["mdblist", "com.mdblist.lists", "MDBList", "mdblist.png",
     "https://1fe84bc728af-stremio-mdblist.baby-beamup.club/manifest.json",
     "https://1fe84bc728af-stremio-mdblist.baby-beamup.club/configure", true, "Browse curated lists"],
    ["anime-kitsu", "community.anime.kitsu", "Anime Kitsu", "anime-kitsu.png",
     "https://anime-kitsu.strem.fun/manifest.json", "", false, "Anime catalogues from Kitsu"]
]

function curated(all, selection) {
    return selection.map(function(entry) {
        var [slug, id, title, logo, manifestUrl, configureUrl, setupRequired, description] = entry
        var live = all.find(function(addon) { return addon.slug === slug })
        if (live) return Object.assign({}, live, { title: title, description: description,
            setupRequired: setupRequired || live.setupRequired })
        var addon = bundled(id, slug, title, logo, manifestUrl, description, configureUrl !== "")
        addon.configureUrl = configureUrl
        addon.setupRequired = setupRequired
        return addon
    })
}

function catalogues() {
    return [
        Object.assign(bundled("colosseum.catalogue.vault", "grand-database", "Colosseum Grand Database",
            "colosseum-grand-database.png", "", "Manga and comics"), { world: "Tankoban", builtIn: true }),
        Object.assign(bundled("colosseum.catalogue.applebooks", "apple-books", "Apple Books",
            "applebooks.ico", "", "Books and authors"), { world: "Biblio", builtIn: true }),
        Object.assign(bundled("com.linvo.cinemeta", "cinemeta", "Cinemeta", "",
            "", "Movies and shows"), { world: "Theatre", builtIn: true }),
        Object.assign(bundled("colosseum.catalogue.anilist", "anilist", "AniList", "anilist.png",
            "", "Manga and series"), { world: "Tankoban", builtIn: true }),
        { slug: "tributes", title: "Tributes", description: "Franchise tributes",
          logo: "../assets/icons/universes-atom.svg", world: "Tributes" },
        { slug: "universes", title: "Universes", description: "Franchise catalogues",
          logo: "../assets/icons/universes-atom.svg", world: "Universes" }
    ]
}

function search(all, query) {
    var words = query.toLowerCase().trim().split(/\s+/).filter(Boolean)
    if (!words.length) return []
    var hits = []
    all.forEach(function(addon) {
        var name = addon.name.toLowerCase()
        var categories = addon.categories.join(" ")
        var description = addon.description.toLowerCase()
        var score = 0
        for (var word of words) {
            if (name.includes(word)) score += 100
            else if (categories.includes(word)) score += 10
            else if (description.includes(word)) score++
            else return
        }
        hits.push({ addon: addon, score: score })
    })
    hits.sort(function(a, b) { return b.score - a.score || b.addon.stars - a.addon.stars })
    return hits.map(function(hit) { return hit.addon })
}

function supportedCategories(all) {
    var supported = ["anime", "asian drama", "bollywood", "debrid support", "http streams",
                     "metadata", "movies", "subtitles", "torrents", "tv shows"]
    return supported.map(function(name) {
        var items = all.filter(function(a) { return has(a, name) })
        var art = items.find(function(a) { return a.logo.length > 0 })
        return { name: name, title: name, count: items.length, cover: art ? art.logo : "", all: items }
    }).filter(function(category) { return category.count > 0 })
}

function essentials(all) {
    var out = []
    for (var i = 0; i < ESSENTIALS.length; i++)
        for (var j = 0; j < all.length; j++)
            if (essentialName(all[j]) === ESSENTIALS[i]) { out.push(all[j]); break }
    return out
}

// [{ title, sub, items, all }] — each row's top add-ons by stars (all = the See-all list), Essentials left out.
function rows(all, showExplicit) {
    var shown = all.filter(function(a) { return visible(a, showExplicit) && ESSENTIALS.indexOf(essentialName(a)) < 0 })
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
