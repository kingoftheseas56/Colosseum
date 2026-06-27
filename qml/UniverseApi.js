// UniverseApi.js — live cross-medium data for a Colosseum UNIVERSE page (cat-1 / anime template).
//
// Re-sourced so every tile is BORN with a real, routable ID (the old Jikan/MAL version showed dead
// covers that linked nowhere, and stalled the page until the slowest of three calls returned):
//   WATCH side  → Cinemeta (v3-cinemeta.strem.io)  — keyless. Series + movie search by franchise
//                 name; each result carries a real Cinemeta id (tt…) + type, so a tile opens A4's
//                 TheatreSeries → Torrentio → player. Same source the Theatre world already uses.
//   READ side   → AniList GraphQL (graphql.anilist.co) — keyless. Manga search by franchise; each
//                 entry carries its AniList id + title, routing into A1's MangaSeries (WeebCentral).
// Both are keyless / no-login (the standing sourcing law). Banners aren't in either API (Oda's
// colour spreads live in the manga) so they stay CURATED per marquee universe; AniList's banner is
// the fallback.
//
// PROGRESSIVE: loadUniverse(name, push) calls `push` once per source as it lands (not once at the
// end) — the banner/blurb/duality paint the instant the first call returns, rows fill in behind.
// Each push hands a FRESH top-level object so QML re-binds (mutating one object in place wouldn't).
.pragma library

var CINEMETA = "https://v3-cinemeta.strem.io";
var ANILIST  = "https://graphql.anilist.co";

// curated banner per marquee universe (keyed lowercase) — the hand-pick; AniList's banner is fallback.
var BANNERS = {
    "one piece": "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg"
};

// warm/cool tints shown while a cover loads
var palette = [ ["#5d4633","#18110c"], ["#33445d","#0c1118"], ["#5b3a64","#170d1b"],
                ["#3f5640","#111b12"], ["#5a3a3f","#160d0b"], ["#3c4a63","#0e121b"] ];
function tone(i) { return palette[i % palette.length]; }

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

function postJson(url, payload, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("POST", url);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader("Accept", "application/json");
    xhr.send(JSON.stringify(payload));
}

function normArt(url) {
    if (!url) return "";
    return String(url)
        .replace("https://images.metahub.space/", "https://live.metahub.space/")
        .replace("/poster/small/", "/poster/medium/");   // upgrade tiny posters; never downgrade large
}

function cleanText(text, fallback) {
    if (!text) return fallback;
    var out = String(text)
        .replace(/<br\s*\/?>/gi, " ").replace(/<[^>]+>/g, " ")   // AniList descriptions are HTML
        .replace(/\(Source:[^)]+\)/g, "").replace(/\s+/g, " ").trim();
    return out.length > 240 ? out.substring(0, 237) + "..." : out;
}

// a Cinemeta meta → a WATCH tile (carries id + type so it routes to TheatreSeries)
function mapWatch(meta, i) {
    var t = tone(i);
    return {
        id: meta.id || "",
        type: meta.type || "movie",
        title: meta.name || meta.title || "Untitled",
        cover: normArt(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : "")),
        art:   normArt(meta.background || (meta.id ? "https://live.metahub.space/background/medium/" + meta.id + "/img" : "")),
        c1: t[0], c2: t[1]
    };
}

// an AniList media → a READ tile (carries AniList id; routes to MangaSeries by title)
function mapRead(m, i) {
    var t = tone(i);
    var title = (m.title && (m.title.english || m.title.romaji)) || "Untitled";
    return {
        id: m.id,
        title: title,
        // extraLarge is AniList's true full-res cover (its `large` is only the medium file)
        cover: (m.coverImage && (m.coverImage.extraLarge || m.coverImage.large)) || "",
        chapters: m.chapters || 0,
        c1: t[0], c2: t[1]
    };
}

// "Manga  ·  12 Anime & Series  ·  15 Films" for the banner
function metaLine(u) {
    var parts = [];
    if (u.manga.length)  parts.push("Manga");
    if (u.anime.length)  parts.push(u.anime.length + (u.anime.length === 1 ? " Series" : " Anime & Series"));
    if (u.movies.length) parts.push(u.movies.length + " Films");
    return parts.join("   ·   ");
}

// keep only Cinemeta hits whose name actually contains the franchise (search is fuzzy)
function relevant(metas, query) {
    var q = query.toLowerCase();
    return metas.filter(function(m) {
        var n = (m.name || m.title || "").toLowerCase();
        return n.indexOf(q) !== -1;
    });
}

// loadUniverse("One Piece", push) — push({ name, blurb, banner, metaline, read, watch,
//                                          manga[], anime[], movies[] }) once per source as it lands.
function loadUniverse(name, push) {
    var out = {
        name: name, blurb: "", banner: BANNERS[name.toLowerCase()] || "", metaline: "",
        read:  { sub: "Start the manga" },
        watch: { sub: "Start watching" },
        manga: [], anime: [], movies: []
    };
    function emit() {
        out.metaline = metaLine(out);
        if (!out.banner && out.anime.length) out.banner = out.anime[0].art || out.anime[0].cover;
        push({                                  // FRESH object each time so QML re-binds
            name: out.name, blurb: out.blurb, banner: out.banner, metaline: out.metaline,
            read: out.read, watch: out.watch,
            manga: out.manga, anime: out.anime, movies: out.movies
        });
    }

    // --- WATCH · series (the anime + any live-action) from Cinemeta ---
    requestJson(CINEMETA + "/catalog/series/top/search=" + encodeURIComponent(name) + ".json",
        function(json) {
            var metas = (json && json.metas) ? relevant(json.metas, name) : [];
            out.anime = metas.slice(0, 18).map(mapWatch);
            if (out.anime.length) {
                var top = out.anime[0];
                out.watch = { id: top.id, type: top.type, title: top.title, cover: top.cover,
                              art: top.art, sub: "Start watching" };
            }
            emit();
        });

    // --- WATCH · films from Cinemeta ---
    requestJson(CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(name) + ".json",
        function(json) {
            var metas = (json && json.metas) ? relevant(json.metas, name) : [];
            out.movies = metas.slice(0, 18).map(mapWatch);
            if (!out.watch.id && out.movies.length) {           // no series? fall back to a film
                var f = out.movies[0];
                out.watch = { id: f.id, type: f.type, title: f.title, cover: f.cover, art: f.art, sub: "Start watching" };
            }
            emit();
        });

    // --- READ · manga from AniList (carries the AniList id) ---
    var query = "query($q:String){Page(perPage:14){media(search:$q,type:MANGA,sort:SEARCH_MATCH)" +
                "{id title{romaji english} coverImage{extraLarge large} bannerImage description(asHtml:false) chapters}}}";
    postJson(ANILIST, { query: query, variables: { q: name } }, function(json) {
        var media = (json && json.data && json.data.Page && json.data.Page.media) ? json.data.Page.media : [];
        out.manga = media.slice(0, 14).map(mapRead);
        if (out.manga.length) {
            var m0 = media[0];
            out.read = { id: m0.id, title: out.manga[0].title, cover: out.manga[0].cover,
                         art: out.manga[0].cover,          // hi-res cover for the big READ panel (cropped)
                         chapters: m0.chapters || 0,
                         sub: m0.chapters ? (m0.chapters + " chapters") : "Start the manga" };
            out.blurb = cleanText(m0.description, out.blurb);
            if (!BANNERS[name.toLowerCase()] && m0.bannerImage) out.banner = m0.bannerImage;
        }
        emit();
    });
}

// every remote cover, for boot prefetch / disk-cache warming
function imageUrls(u) {
    var urls = [], groups = [u.manga, u.anime, u.movies];
    if (u.banner) urls.push(u.banner);
    for (var g = 0; g < groups.length; g++)
        for (var i = 0; i < groups[g].length; i++)
            if (groups[g][i].cover && urls.indexOf(groups[g][i].cover) === -1)
                urls.push(groups[g][i].cover);
    return urls;
}
