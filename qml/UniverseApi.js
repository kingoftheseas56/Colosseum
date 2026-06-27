// UniverseApi.js - live cross-medium data for a Colosseum UNIVERSE page (cat-1 / anime template).
// Source: MAL via Jikan (api.jikan.moe) - NO key, NO login (the standing sourcing law). Given a
// universe name it pulls the franchise's anime + manga entries and BUCKETS them into the page rows:
//   Anime    = TV + ONA      (the series, incl. remakes)
//   Movies   = Movie
//   Specials = Special / TV Special / OVA
//   Manga    = Manga / One-shot / Light Novel / Novel   (Doujinshi dropped)
//   dropped  = CM / PV / Music  (ads, promos, MVs - pure noise)
// The Manga row is the LISTING + routing target only; the actual reader is A1's MangaSeries.qml
// (WeebCentral). Banners aren't in any API (Oda's colour spreads live in the manga) so they are
// CURATED per marquee universe - BANNERS{} holds the hand-pick; AniList banner is the fallback.
// Raw MAL data is noisy (arc re-edits show as "TV", recaps as specials); the marquee's hand-craft
// layer is where that gets cleaned - this module surfaces the honest raw buckets.
//
// Mirrors the TheatreApi.js / BiblioApi.js adapter pattern (.pragma library, XMLHttpRequest).
.pragma library

var JIKAN = "https://api.jikan.moe/v4";
var TVMAZE = "https://api.tvmaze.com";   // keyless TV - feeds the live-action "Shows" row

// curated banner per marquee universe (keyed lowercase). The colour-spread pool is hand-picked;
// this is the keyless fallback (AniList's single banner) until the curator wires the rotating set.
var BANNERS = {
    "one piece": "https://s4.anilist.co/file/anilistcdn/media/manga/banner/30013-hbbRZqC5MjYh.jpg"
};

// warm/cool tints shown while a cover loads (same role as Theatre's palette)
var palette = [ ["#5d4633","#18110c"], ["#33445d","#0c1118"], ["#5b3a64","#170d1b"],
                ["#3f5640","#111b12"], ["#5a3a3f","#160d0b"], ["#3c4a63","#0e121b"] ];
function tone(i) { return palette[i % palette.length]; }

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE)
            return;
        if (xhr.status < 200 || xhr.status >= 300) {
            done(null);
            return;
        }
        try {
            done(JSON.parse(xhr.responseText));
        } catch (e) {
            done(null);
        }
    };
    xhr.open("GET", url);
    xhr.send();
}

function cleanText(text, fallback) {
    if (!text)
        return fallback;
    var out = String(text).replace(/\s+/g, " ").replace(/\(Source:[^)]+\)/g, "").trim();
    return out.length > 240 ? out.substring(0, 237) + "..." : out;
}

function mapEntry(e, i) {
    var t = tone(i);
    var jpg = (e.images && e.images.jpg) ? e.images.jpg : {};
    var yr = e.year || (e.aired && e.aired.prop && e.aired.prop.from ? e.aired.prop.from.year : null);
    return {
        id: e.mal_id,
        title: e.title,
        cover: jpg.large_image_url || jpg.image_url || "",
        type: e.type || "",
        year: yr,
        c1: t[0],
        c2: t[1]
    };
}

// Jikan search is fuzzy - keep only entries whose title actually contains the franchise name.
function relevant(list, query) {
    var q = query.toLowerCase();
    return list.filter(function(e) {
        return e.title && e.title.toLowerCase().indexOf(q) !== -1;
    });
}

// "Manga  ·  2 Anime  ·  10 Films  ·  Specials" for the banner
function metaLine(u) {
    var parts = [];
    if (u.manga.length)    parts.push("Manga");
    if (u.anime.length)    parts.push(u.anime.length + " Anime");
    if (u.movies.length)   parts.push(u.movies.length + " Films");
    if (u.specials.length) parts.push("Specials");
    if (u.shows && u.shows.length) parts.push("Live-Action");
    return parts.join("   ·   ");
}

// loadUniverse("One Piece", fn) -> fn({ name, blurb, banner, metaline, read, watch,
//                                       manga[], anime[], specials[], movies[] })
function loadUniverse(name, done) {
    var out = {
        name: name,
        blurb: "",
        banner: BANNERS[name.toLowerCase()] || "",
        metaline: "",
        read: { sub: "Start the manga" },
        watch: { sub: "Start the anime" },
        manga: [], anime: [], specials: [], movies: [], shows: []
    };
    var pending = 3;
    function finish() {
        pending -= 1;
        if (pending === 0) {
            out.metaline = metaLine(out);
            if (!out.banner && out.anime.length)
                out.banner = out.anime[0].cover;   // last-ditch fallback
            done(out);
        }
    }

    // --- anime side: series / specials / movies ---
    requestJson(JIKAN + "/anime?q=" + encodeURIComponent(name) + "&limit=25",
        function(json) {
            var data = (json && json.data) ? relevant(json.data, name) : [];
            var ai = 0, si = 0, mi = 0, main = null, mainPop = -1;
            for (var k = 0; k < data.length; k++) {
                var e = data[k], type = e.type || "";
                if (type === "TV" || type === "ONA") {
                    // canonical series = the TV entry with the most members (not just the first)
                    if (type === "TV" && (e.members || 0) > mainPop) { main = e; mainPop = e.members || 0; }
                    out.anime.push(mapEntry(e, ai++));
                } else if (type === "Movie") {
                    out.movies.push(mapEntry(e, mi++));
                } else if (type === "Special" || type === "TV Special" || type === "OVA") {
                    out.specials.push(mapEntry(e, si++));
                }
                // CM / PV / Music dropped as noise
            }
            if (!main && out.anime.length && data.length) main = data[0];
            if (main) {
                out.blurb = cleanText(main.synopsis, "");
                var ajpg = (main.images && main.images.jpg) ? main.images.jpg : {};
                out.watch = { episodes: main.episodes,
                              sub: main.episodes ? (main.episodes + " episodes") : "Start the anime",
                              cover: ajpg.large_image_url || ajpg.image_url || "" };
            }
            finish();
        });

    // --- manga side: listing + routing target (reader is A1's WeebCentral / MangaSeries.qml) ---
    requestJson(JIKAN + "/manga?q=" + encodeURIComponent(name) + "&limit=15",
        function(json) {
            var data = (json && json.data) ? relevant(json.data, name) : [];
            var keep = { "Manga": 1, "One-shot": 1, "Light Novel": 1, "Novel": 1, "Manhwa": 1, "Manhua": 1 };
            var mi = 0, main = null, mainPop = -1;
            for (var k = 0; k < data.length; k++) {
                var e = data[k];
                if (!keep[e.type]) continue;       // drop Doujinshi etc.
                if (e.type === "Manga" && (e.members || 0) > mainPop) { main = e; mainPop = e.members || 0; }
                out.manga.push(mapEntry(e, mi++));
            }
            if (main) {
                var mjpg = (main.images && main.images.jpg) ? main.images.jpg : {};
                out.read = { chapters: main.chapters,
                             sub: main.chapters ? (main.chapters + " chapters") : "Start the manga",
                             cover: mjpg.large_image_url || mjpg.image_url || "" };
                if (!out.blurb) out.blurb = cleanText(main.synopsis, "");
            }
            finish();
        });

    // --- live-action: TVmaze (keyless TV). Drop the anime (type "Animation"); keep live-action. ---
    requestJson(TVMAZE + "/search/shows?q=" + encodeURIComponent(name),
        function(json) {
            var arr = (json && json.length) ? json : [];
            var q = name.toLowerCase(), si = 0;
            for (var k = 0; k < arr.length; k++) {
                var s = arr[k].show;
                if (!s || !s.name || s.name.toLowerCase().indexOf(q) === -1) continue;
                if (s.type === "Animation") continue;
                var t = tone(si++);
                out.shows.push({
                    id: s.id, title: s.name,
                    cover: s.image ? (s.image.original || s.image.medium) : "",
                    type: s.type || "",
                    year: s.premiered ? parseInt(s.premiered.substring(0, 4)) : null,
                    c1: t[0], c2: t[1]
                });
            }
            finish();
        });
}

// every remote cover, for boot prefetch / disk-cache warming (mirrors Theatre's helper)
function imageUrls(u) {
    var urls = [], groups = [u.manga, u.anime, u.specials, u.movies];
    if (u.banner) urls.push(u.banner);
    for (var g = 0; g < groups.length; g++)
        for (var i = 0; i < groups[g].length; i++)
            if (groups[g][i].cover && urls.indexOf(groups[g][i].cover) === -1)
                urls.push(groups[g][i].cover);
    return urls;
}
