// ExtensionsCatalog.js — the store's shelf data: curated rails (Harbor's list,
// minus adult — the house rule, applied at the data layer, not a toggle) and the
// community registry (stremio-addons.net, with Stremio's official collection as
// the fallback well). Ratified mock: agents/colosseum-extensions-mock.html;
// spec: docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md.
.pragma library

var COMMUNITY_API = "https://stremio-addons.net/api/v0";
var OFFICIAL_COLLECTION = "https://api.strem.io/addonsofficialcollection.json";

function _get(url, done) {
    var xhr = new XMLHttpRequest();
    var settled = false;
    function finish(v) { if (!settled) { settled = true; done(v); } }
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { finish(null); return; }
        try { finish(JSON.parse(xhr.responseText)); } catch (e) { finish(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

// ---- the adult wall: nothing past this line reaches the UI ----
function _isAdult(entry) {
    if (!entry) return true;
    if (entry.nsfw) return true;
    var m = entry.manifest || entry;
    if (m.behaviorHints && m.behaviorHints.adult) return true;
    var cats = entry.categories || [];
    for (var i = 0; i < cats.length; i++) {
        var c = String(cats[i]).toLowerCase();
        if (c === "nsfw" || c === "adult") return true;
    }
    return false;
}

// =====================================================================
// Curated rails — ported from Harbor src/lib/addons-store/curated.ts,
// adult entries dropped. tone1/tone2 = the card monogram gradient.
// =====================================================================

var FEATURED = {
    id: "com.stremio.torrentio.addon", name: "Torrentio",
    line: "Twelve indexers behind one door — every play gets its pick of sources.",
    facts: "gives play sources · movies, shows & anime · configurable · by the Stremio community",
    url: "https://torrentio.strem.fun/manifest.json"
};

var RAILS = [
    {
        key: "essentials", title: "The essentials",
        count: "the four the house already runs on",
        hint: "these came pre-installed — the store just makes them visible",
        items: [
            { id: "com.linvo.cinemeta", name: "Cinemeta",
              desc: "The canonical catalog — every movie and show row Theatre wakes up with.",
              kind: "catalog & details · movies, shows",
              url: "https://v3-cinemeta.strem.io/manifest.json",
              tone1: "#23303f", tone2: "#101820", core: true },
            { id: "com.stremio.torrentio.addon", name: "Torrentio",
              desc: "Play sources from twelve indexers, sorted by quality and health.",
              kind: "play sources · movies, shows, anime",
              url: "https://torrentio.strem.fun/manifest.json",
              tone1: "#2d2a1c", tone2: "#181405" },
            { id: "community.anime.kitsu", name: "Anime Kitsu",
              desc: "The anime shelf's brain — proper seasons, splits and episode orders.",
              kind: "catalog & details · anime",
              url: "https://anime-kitsu.strem.fun/manifest.json",
              tone1: "#3a2530", tone2: "#191019" },
            { id: "org.stremio.opensubtitlesv3", name: "OpenSubtitles v3",
              desc: "Subtitles in every language, matched to the exact episode playing.",
              kind: "subtitles · everything",
              url: "https://opensubtitles-v3.strem.io/manifest.json",
              tone1: "#233a33", tone2: "#0f1a17" }
        ]
    },
    {
        key: "streams", title: "More play sources",
        count: "6 recommended",
        hint: "each one adds its own answers when you press play",
        items: [
            { id: "comet.elfhosted.com", name: "Comet",
              desc: "Fast, filterable torrent sources with clean quality labels.",
              kind: "play sources · movies, shows",
              url: "https://comet.elfhosted.com/manifest.json",
              tone1: "#26324a", tone2: "#0e1420" },
            { id: "stremio.addons.mediafusion|elfhosted", name: "MediaFusion",
              desc: "A wide net — torrents, usenet and live feeds under one roof.",
              kind: "play sources · movies, shows, sport",
              url: "https://mediafusion.elfhosted.com/manifest.json",
              tone1: "#33283f", tone2: "#161020" },
            { id: "com.aiostreams.viren070", name: "AIOStreams",
              desc: "Many source addons wrapped into one, with your own ranking rules.",
              kind: "play sources · configurable",
              url: "https://aiostreams.elfhosted.com/stremio/manifest.json",
              tone1: "#40372a", tone2: "#1c1712" },
            { id: "com.keopps.peerflix", name: "Peerflix",
              desc: "Lean torrent sources with a reputation for just working.",
              kind: "play sources · movies, shows",
              url: "https://peerflix.mov/manifest.json",
              tone1: "#2b3038", tone2: "#101318" },
            { id: "com.notorrent.addon", name: "NoTorrent",
              desc: "Direct HTTP streams — no torrents involved at all.",
              kind: "play sources · movies, shows",
              url: "https://addon.notorrent2.workers.dev/manifest.json",
              tone1: "#42332a", tone2: "#1a120d" },
            { id: "webstreamr-mbg", name: "WebStreamr",
              desc: "Free web streams gathered from open hosts.",
              kind: "play sources · movies, shows",
              url: "https://87d6a6ef6b58-webstreamrmbg.baby-beamup.club/manifest.json",
              tone1: "#33242b", tone2: "#150e12" }
        ]
    },
    {
        key: "catalogs", title: "New shelves & catalogs",
        count: "6 recommended",
        hint: "each adds rows to Theatre's home and explore pages",
        items: [
            { id: "pw.ers.netflix-catalog", name: "Netflix Catalog",
              desc: "What's new and leaving on Netflix, as browsable rows.",
              kind: "catalog · movies, shows",
              url: "https://7a82163c306e-stremio-netflix-catalog-addon.baby-beamup.club/manifest.json",
              tone1: "#3f2330", tone2: "#1c0d14" },
            { id: "default.global.topstreaming.flixpatrol", name: "FlixPatrol Top 10",
              desc: "The world's actual top-ten charts, updated daily.",
              kind: "catalog · movies, shows",
              url: "https://top-streaming.stream/username=temporary_username/manifest.json",
              tone1: "#233043", tone2: "#0d1420" },
            { id: "org.stremio.aiolists", name: "AIOLists",
              desc: "Your own watchlists from anywhere, turned into home rows.",
              kind: "catalog · configurable",
              url: "https://aiolists.elfhosted.com/manifest.json",
              tone1: "#37282f", tone2: "#170f13" },
            { id: "com.joaogonp.marveladdon", name: "Marvel Universe",
              desc: "Every Marvel film and show in release or story order.",
              kind: "catalog · movies, shows",
              url: "https://addon-marvel.onrender.com/manifest.json",
              tone1: "#2a3550", tone2: "#141a2c" },
            { id: "com.tapframe.dcaddon", name: "DC Universe",
              desc: "The DC canon, ordered and browsable.",
              kind: "catalog · movies, shows",
              url: "https://addon-dc-cq85.onrender.com/manifest.json",
              tone1: "#1f3a3a", tone2: "#0c1717" },
            { id: "community.morelikethis", name: "More Like This",
              desc: "A \"similar titles\" row on every detail page.",
              kind: "details · movies, shows",
              url: "https://bbab4a35b833-more-like-this.baby-beamup.club/manifest.json",
              tone1: "#3a3226", tone2: "#171208" }
        ]
    },
    {
        key: "extras", title: "Subtitles & extras",
        count: "6 recommended",
        hint: "",
        items: [
            { id: "community.subsource.subtitles", name: "SubSource",
              desc: "A second deep well of subtitles when the first runs dry.",
              kind: "subtitles · everything",
              url: "https://subsource.strem.top/manifest.json",
              tone1: "#2c3a2c", tone2: "#101a10" },
            { id: "community.subdl.subtitles", name: "SubDL",
              desc: "Community subtitles with strong regional coverage.",
              kind: "subtitles · everything",
              url: "https://subdl.strem.top/manifest.json",
              tone1: "#32323f", tone2: "#131318" },
            { id: "com.stremio.rtngz", name: "Ratings",
              desc: "IMDb, Rotten Tomatoes and Letterboxd scores on every page.",
              kind: "details · movies, shows",
              url: "https://72059fbbd1e5-stremio-addon-ratings.baby-beamup.club/manifest.json",
              tone1: "#3f3623", tone2: "#181307" },
            { id: "org.streailer.trailer", name: "Streailer",
              desc: "Trailers on demand for anything in the catalog.",
              kind: "extras · movies, shows",
              url: "https://streailer.elfhosted.com/manifest.json",
              tone1: "#23303f", tone2: "#0d141c" },
            { id: "community.meteor", name: "Meteor",
              desc: "A second opinion for anime — alternate catalog and sources.",
              kind: "catalog & sources · anime",
              url: "https://meteorfortheweebs.midnightignite.me/stremio/manifest.json",
              tone1: "#3a2a3a", tone2: "#170f17" },
            { id: "community.usatv", name: "USA TV",
              desc: "Live channels, playable like anything else.",
              kind: "live tv · channels",
              url: "https://848b3516657c-usatv.baby-beamup.club/manifest.json",
              tone1: "#2a3843", tone2: "#0e161b" }
        ]
    }
];

function rails() { return RAILS; }
function featured() { return FEATURED; }

// =====================================================================
// Community browse — stremio-addons.net first, Stremio's official
// collection as the fallback. Both parsed defensively (fields differ),
// both passed through the adult wall. done(list) — list may be [].
// Row shape: { id, name, desc, kind, url, stars, tone1, tone2 }
// =====================================================================

var _tones = [
    ["#26324a", "#0e1420"], ["#33283f", "#161020"], ["#2d2a1c", "#181405"],
    ["#3a2530", "#191019"], ["#233a33", "#0f1a17"], ["#40372a", "#1c1712"],
    ["#2b3038", "#101318"], ["#3f2330", "#1c0d14"], ["#233043", "#0d1420"]
];

// ─────────────────────────────────────────────────────────────────────────────
// WORLDS — derived from the manifest's own `types`, never stored. Extending the
// type vocabulary is all it takes, and ExtensionsStore.slimManifest keeps `types`
// verbatim, so no C++ store change and no installed.json migration is needed.
// One extension can legitimately serve TWO worlds: Torrent Indexers feeds comics
// AND books AND audiobooks from a single install.  (spec §3.2)
// ─────────────────────────────────────────────────────────────────────────────
var WORLD_TYPES = {
    theatre:  ["movie", "series", "anime"],
    tankoban: ["manga", "comic"],
    biblio:   ["book", "audiobook"]
};

// A universe extension is classified by ROLE, not by content: it declares types
// across every world but aggregates an IP rather than providing any medium, so it
// belongs to exactly one tab. Checked BEFORE the type derivation, or a universe
// would appear in all three worlds plus its own — four rows, one stored `enabled`
// flag.  (Agent 0's ruling, universes design §5.1a)
function _hasResource(manifest, want) {
    var res = (manifest && manifest.resources) || [];
    for (var i = 0; i < res.length; i++) {
        var n = typeof res[i] === "string" ? res[i] : (res[i] && res[i].name) || "";
        if (n === want) return true;
    }
    return false;
}

// Every world this installed entry belongs to, as an array. Empty = belongs nowhere
// (e.g. a subtitles-only addon declaring no world type) — callers decide whether to
// show it under its declaring world or not at all.
function worldsFor(entry) {
    var m = (entry && entry.manifest) || entry || {};
    if (_hasResource(m, "universe")) return ["universes"];
    var types = m.types || [];
    var out = [];
    for (var w in WORLD_TYPES) {
        var want = WORLD_TYPES[w];
        for (var i = 0; i < want.length; i++)
            if (types.indexOf(want[i]) !== -1) { out.push(w); break; }
    }
    return out;
}

function inWorld(entry, world) {
    return worldsFor(entry).indexOf(world) !== -1;
}

// Is this row a catalogue (what fills the shelves) or a well (what fetches)?
// Catalogues are core+catalog; wells provide `stream`.  (spec §3.1)
function isCatalogue(entry) {
    return entry && entry.core === true && _hasResource(entry.manifest || entry, "catalog");
}
function isWell(entry) {
    return !isCatalogue(entry) && _hasResource((entry && entry.manifest) || entry, "stream");
}

// ─────────────────────────────────────────────────────────────────────────────
// The human-readable JOB of a house well, keyed by well id — deliberately a static
// table and NOT a manifest field: slimManifest keeps only a fixed key allowlist, so
// a custom field would survive seeding and be silently dropped on re-install. It
// also can't be derived from `types`, because two wells can share one type
// (WeebCentral "chapter pages" and Nyaa "volume torrents" are both `manga`).
// Mirrors AddonLogos.js's table pattern.  (spec §3.3, and A5's own spec self-review)
// ─────────────────────────────────────────────────────────────────────────────
var JOB = {
    "colosseum.well.nyaa":              "volume torrents",
    "colosseum.well.weebcentral.pages": "chapter pages",
    "colosseum.well.getcomics.issues":  "issue downloads",
    "colosseum.well.libgen":            "book files",
    // Three indexers, not four (Knaben is off in TankorentSearchService), and the search
    // is only ever asked for books and comics — never audiobooks. 2026-07-26 ground-truth.
    "colosseum.well.indexers":          "comic and book torrents",
    "colosseum.well.audiobookbay":      "audiobook torrents"
};

function jobFor(id) { return JOB[id] || ""; }

function _kindLine(manifest, categories, id) {
    // A house well's job wins over the resource-derived vocabulary.
    var job = jobFor(id || (manifest && manifest.id) || "");
    if (job) return job;
    var res = (manifest && manifest.resources) || [];
    var names = [];
    for (var i = 0; i < res.length; i++)
        names.push(typeof res[i] === "string" ? res[i] : (res[i] && res[i].name) || "");
    var parts = [];
    if (names.indexOf("stream") !== -1) parts.push("play sources");
    if (names.indexOf("catalog") !== -1) parts.push("catalog");
    if (names.indexOf("meta") !== -1) parts.push("details");
    if (names.indexOf("subtitles") !== -1) parts.push("subtitles");
    if (!parts.length && categories && categories.length)
        parts.push(String(categories[0]).toLowerCase());
    return parts.join(" & ") || "extension";
}

function _rowFrom(entry, i) {
    var m = entry.manifest || entry;
    // Prefer the real manifest over the directory page. stremio-addons.net's v0 API
    // returns transportUrl=null and `url` = the human listing page (…/addons/<slug>),
    // which 404s on install because ExtensionsStore.normalizeUrl appends /manifest.json
    // to a page. The installable manifest is in `manifestUrl`. (Confirmed live 2026-07-24.)
    var url = entry.transportUrl || entry.manifestUrl || entry.url || "";
    var name = m.name || entry.name || "";
    if (!name || !url) return null;
    var t = _tones[i % _tones.length];
    var logo = (m && m.logo) || entry.logo || entry.icon || "";
    if (typeof logo !== "string" || logo.indexOf("data:") === 0) logo = "";
    return {
        id: m.id || entry.id || url,
        name: name,
        desc: (m.description || entry.description || "").split("\n")[0].slice(0, 140),
        kind: _kindLine(m, entry.categories),
        url: url,
        logo: logo,
        stars: entry.stars || entry.votes || 0,
        tone1: t[0], tone2: t[1]
    };
}

function _mapList(raw) {
    var arr = null;
    if (Array.isArray(raw)) arr = raw;
    else if (raw && Array.isArray(raw.addons)) arr = raw.addons;
    else if (raw && Array.isArray(raw.items)) arr = raw.items;
    else if (raw && Array.isArray(raw.data)) arr = raw.data;
    else if (raw && Array.isArray(raw.result)) arr = raw.result;
    if (!arr) return null;
    var out = [];
    for (var i = 0; i < arr.length; i++) {
        var entry = arr[i];
        if (_isAdult(entry)) continue;
        var row = _rowFrom(entry, out.length);
        if (row) out.push(row);
    }
    return out;
}

// sort: "top" | "new" | "rising"; search: free text or ""
function browse(sort, search, done) {
    var qs = "?page=1&limit=40&nsfw=exclude&order=desc&sort_by="
           + (sort === "new" ? "createdAt" : "stars");
    if (search) qs += "&search=" + encodeURIComponent(search);
    var url = COMMUNITY_API + (sort === "rising" && !search ? "/rising" : "/addons" + qs);
    _get(url, function(raw) {
        var list = _mapList(raw);
        if (list && list.length) { done(list); return; }
        // registry down or shape drifted — fall back to Stremio's official collection
        _get(OFFICIAL_COLLECTION, function(rawOfficial) {
            var official = _mapList(rawOfficial) || [];
            if (search) {
                var q = search.toLowerCase();
                official = official.filter(function(r) {
                    return (r.name + " " + r.desc).toLowerCase().indexOf(q) !== -1;
                });
            }
            done(official);
        });
    });
}
