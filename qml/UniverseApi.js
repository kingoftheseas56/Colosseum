// UniverseApi.js — live cross-medium data for a Colosseum UNIVERSE page (the generic template).
//
// Re-sourced so every tile is BORN with a real, routable ID (the old Jikan/MAL version showed dead
// covers that linked nowhere, and stalled the page until the slowest of three calls returned):
//   WATCH side  → Cinemeta (v3-cinemeta.strem.io)  — keyless. Series + movie search by franchise
//                 name; each result carries a real Cinemeta id (tt…) + type, so a tile opens A4's
//                 TheatreSeries → Torrentio → player. Same source the Theatre world already uses.
//   READ side   → AniList GraphQL (graphql.anilist.co) — keyless. Manga search by franchise; each
//                 entry carries its AniList id + title, routing into A1's MangaSeries (WeebCentral).
// Both are keyless / no-login (the standing sourcing law).
//
// CURATION rides Universes.js (the ONE curation point — 2026-07-12 expansion): per-universe banner,
// blurb, and the optional search hints — seriesQueries / movieQueries / readQueries — that let a
// universe whose NAME isn't a searchable term (DC Animated Universe, A Song of Ice and Fire,
// Weekly Shonen Jump) assemble its page from the right titles. Multi-query results merge
// id-deduped via mergeById (pure — headless-tested in tests/universe_api_merge_harness.qml).
//
// PROGRESSIVE: loadUniverse(name, push) calls `push` once per source RESPONSE as it lands (not
// once at the end) — the banner/blurb/duality paint the instant the first call returns, rows fill
// in behind. Each push hands a FRESH top-level object so QML re-binds (mutating one object in
// place wouldn't).
.pragma library
.import "Universes.js" as UDB
.import "ComicsApi.js" as ComicsApi

var CINEMETA = "https://v3-cinemeta.strem.io";
var ANILIST  = "https://graphql.anilist.co";

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

// merge `incoming` into `existing`, skipping ids already present, capped. PURE — the
// multi-query assembly rides this (query responses arrive in any order; a title hit by
// two queries must appear once).
function mergeById(existing, incoming, cap) {
    var out = existing.slice();
    var seen = {};
    for (var i = 0; i < out.length; i++) seen[out[i].id] = true;
    for (var j = 0; j < (incoming || []).length; j++) {
        if (out.length >= cap) break;
        var it = incoming[j];
        if (!it || !it.id || seen[it.id]) continue;
        seen[it.id] = true;
        out.push(it);
    }
    return out;
}

// loadUniverse("One Piece", push) — push({ name, blurb, banner, metaline, read, watch,
//                                          manga[], anime[], movies[] }) once per response.
function loadUniverse(name, push) {
    var cfg = UDB.configFor(name);
    var out = {
        name: name,
        blurb: cfg.blurb || "",                 // curated copy first; AniList only fills a gap
        banner: cfg.banner || "",               // curated art first; anime art is the fallback
        metaline: "",
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

    var seriesQueries = (cfg.seriesQueries && cfg.seriesQueries.length) ? cfg.seriesQueries : [name];
    var movieQueries  = (cfg.movieQueries && cfg.movieQueries.length) ? cfg.movieQueries : [name];
    var readQueries   = (cfg.readQueries && cfg.readQueries.length) ? cfg.readQueries : [name];

    // --- WATCH · series from Cinemeta (one search per curated query, merged id-deduped) ---
    seriesQueries.forEach(function(q) {
        requestJson(CINEMETA + "/catalog/series/top/search=" + encodeURIComponent(q) + ".json",
            function(json) {
                var metas = (json && json.metas) ? relevant(json.metas, q) : [];
                out.anime = mergeById(out.anime, metas.map(mapWatch), 18);
                if (!out.watch.id && out.anime.length) {
                    var top = out.anime[0];
                    out.watch = { id: top.id, type: top.type, title: top.title, cover: top.cover,
                                  art: top.art, sub: "Start watching" };
                }
                emit();
            });
    });

    // --- WATCH · films from Cinemeta (same multi-query merge) ---
    movieQueries.forEach(function(q) {
        requestJson(CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(q) + ".json",
            function(json) {
                var metas = (json && json.metas) ? relevant(json.metas, q) : [];
                out.movies = mergeById(out.movies, metas.map(mapWatch), 18);
                if (!out.watch.id && out.movies.length) {           // no series? fall back to a film
                    var f = out.movies[0];
                    out.watch = { id: f.id, type: f.type, title: f.title, cover: f.cover, art: f.art, sub: "Start watching" };
                }
                emit();
            });
    });

    // --- READ · manga from AniList (one search per curated query; the magazine = its flagships) ---
    var gql = "query($q:String){Page(perPage:14){media(search:$q,type:MANGA,sort:SEARCH_MATCH)" +
              "{id title{romaji english} coverImage{extraLarge large} bannerImage description(asHtml:false) chapters}}}";
    readQueries.forEach(function(q) {
        postJson(ANILIST, { query: gql, variables: { q: q } }, function(json) {
            var media = (json && json.data && json.data.Page && json.data.Page.media) ? json.data.Page.media : [];
            // multi-query universes: take the top TWO per query (the flagship + its sequel) so
            // ten flagship queries fill the row with ten DIFFERENT manga — not fourteen
            // spin-offs of whichever query answered first.
            var take = readQueries.length > 1 ? media.slice(0, 2) : media.slice(0, 14);
            out.manga = mergeById(out.manga, take.map(mapRead), 20);
            if (!out.read.id && out.manga.length) {
                var m0 = media[0];
                out.read = { id: out.manga[0].id, title: out.manga[0].title, cover: out.manga[0].cover,
                             art: out.manga[0].cover,          // hi-res cover for the big READ panel (cropped)
                             chapters: (m0 && m0.chapters) || 0,
                             sub: (m0 && m0.chapters) ? (m0.chapters + " chapters") : "Start the manga" };
                if (!out.blurb) out.blurb = cleanText(m0 && m0.description, out.blurb);
                if (!out.banner && m0 && m0.bannerImage) out.banner = m0.bannerImage;
            }
            emit();
        });
    });
}

// loadMangaOnly(name, push) — the MAGAZINE template's loader: ONLY the READ side (Weekly
// Shonen Jump publishes manga — no anime/film queries fired at all, Hemanth 2026-07-12).
// Same progressive multi-query AniList assembly as loadUniverse's read block.
function loadMangaOnly(name, push) {
    var cfg = UDB.configFor(name);
    var out = { name: name, blurb: cfg.blurb || "", banner: cfg.banner || "", manga: [] };
    function emit() {
        push({ name: out.name, blurb: out.blurb, banner: out.banner, manga: out.manga });
    }
    var readQueries = (cfg.readQueries && cfg.readQueries.length) ? cfg.readQueries : [name];
    var gql = "query($q:String){Page(perPage:14){media(search:$q,type:MANGA,sort:SEARCH_MATCH)" +
              "{id title{romaji english} coverImage{extraLarge large} bannerImage description(asHtml:false) chapters}}}";
    // the lineup's RANK is the curated query order, not network arrival order — each
    // query's flagship lands in ITS slot, however late its response comes home
    var slots = new Array(readQueries.length);
    readQueries.forEach(function(q, qi) {
        postJson(ANILIST, { query: gql, variables: { q: q } }, function(json) {
            var media = (json && json.data && json.data.Page && json.data.Page.media) ? json.data.Page.media : [];
            if (media.length) slots[qi] = mapRead(media[0], qi);
            out.manga = slots.filter(function(m) { return !!m; });
            emit();
        });
    });
    emit();   // masthead paints instantly from curation
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

// Resolve a cover URL for a payload entry by its {provider, id} (or kind for comics). The
// static universe payload carries routable IDs but no art — the arc's thesis was "every tile
// BORN with a real ID"; this fetches the cover BACK from that ID so manga/novels/comics aren't
// left on the honest lettered plate. By-ID (precise), never by-title search (which mismatches).
//   anilist    → AniList GraphQL Media(id) coverImage.extraLarge/large
//   applebooks → iTunes lookup?id=<store trackId> artwork (upscaled 100→600)
//   comic      → ComicsApi.posterFor(title + " comic") — the same call ComicSeries makes
function coverFor(entry, kind, done) {
    var e = entry || ({});
    if (e.cover) { done(e.cover); return; }                   // curated / already resolved
    var provider = e.provider || (kind === "comic" ? "getcomics" : "");
    if (provider === "anilist" && e.id) {
        var gql = "query($id:Int){Media(id:$id,type:MANGA){coverImage{extraLarge large}}}";
        postJson(ANILIST, { query: gql, variables: { id: Number(e.id) } }, function(json) {
            var ci = json && json.data && json.data.Media && json.data.Media.coverImage;
            done(ci ? (ci.extraLarge || ci.large || "") : "");
        });
        return;
    }
    if (provider === "applebooks" && e.id) {
        requestJson("https://itunes.apple.com/lookup?id=" + encodeURIComponent(e.id), function(json) {
            var r = json && json.results && json.results[0];
            var art = r ? (r.artworkUrl100 || r.artworkUrl60 || "") : "";
            if (art) art = String(art).replace(/100x100bb/, "600x600bb").replace(/100x100/, "600x600");
            done(art);
        });
        return;
    }
    if (provider === "weebcentral" && e.id) {
        // WeebCentral's cover CDN is a constructible URL (no fetch) — same source the reader uses.
        done("https://temp.compsci88.com/cover/fallback/" + e.id + ".jpg");
        return;
    }
    if (provider === "getcomics" || kind === "comic") {
        // Comics aren't iTunes ebooks (posterFor returns nothing for them) — use the GetComics
        // post's own og_image, the SAME source ComicSeries uses (ComicsApi.postsById). The first
        // post's cover stands in for the series.
        var posts = e.posts || [];
        if (!posts.length) { done(""); return; }
        var key = String(posts[0]);
        ComicsApi.postsById([posts[0]], function(map) {
            done((map && map[key] && map[key].cover) || "");
        });
        return;
    }
    done("");
}
