// TheatreGenreApi.js — genre data for the THEATRE lane (movies / shows / anime). Theatre-owned
// clone of the manga lane's GenreApi/GenreIndexApi pattern (lane discipline: those files are
// A1/A5 territory and stay untouched). Sources, per the standing no-login law:
//   anime          → Jikan (MAL) — live genre ids + counts from /genres/anime, cards from /anime
//   movie / series → Cinemeta top catalogs (catalog/<type>/top/genre=<G>) — lean cards
// loadGenre(kind, name, sort, push) → { count, desc, cards, montage }  (count 0 ⇒ page hides it)
// loadGroups(kind, includeExplicit, done) → GenreIndex-shaped groups [{ name, sub, genres:[tile] }]
.pragma library

var JIKAN = "https://api.jikan.moe/v4";
var CINEMETA_CATALOGS = "https://cinemeta-catalogs.strem.io/top";
var CINEMETA = "https://v3-cinemeta.strem.io";

var MOVIE_GENRES = ["Action", "Drama", "Comedy", "Sci-Fi", "Thriller", "Horror", "Romance",
                    "Animation", "Adventure", "Crime", "Mystery", "Fantasy", "Documentary"];
var SHOW_GENRES  = ["Drama", "Comedy", "Crime", "Sci-Fi", "Thriller", "Mystery", "Action",
                    "Animation", "Adventure", "Fantasy", "Documentary", "Romance", "Horror"];
var ANIME_SIBLINGS = ["Action", "Adventure", "Comedy", "Drama", "Fantasy", "Horror",
                      "Mystery", "Romance", "Sci-Fi", "Slice of Life", "Sports", "Supernatural"];

// MAL's anime.php section membership (mirrors GenreIndexApi's manga structure; anything
// returned by /genres/anime that isn't listed here renders under Themes).
var ANIME_GENRES = ["Action", "Adventure", "Avant Garde", "Award Winning", "Boys Love", "Comedy",
                    "Drama", "Fantasy", "Girls Love", "Gourmet", "Horror", "Mystery", "Romance",
                    "Sci-Fi", "Slice of Life", "Sports", "Supernatural", "Suspense"];
var ANIME_EXPLICIT = ["Ecchi", "Erotica", "Hentai"];
var ANIME_DEMOGRAPHICS = ["Shounen", "Shoujo", "Seinen", "Josei", "Kids"];

// editorial standfirst per genre — the hero's "what this genre IS" line, film-flavored.
var GENRE_DESC = {
    "Action": "Momentum as storytelling. Action lives in the set piece — the chase, the fight, the escape — and in who someone becomes when everything is moving.",
    "Adventure": "The road out of the known world. Adventure is scale and discovery — strange places, high stakes, and the person the journey makes of you.",
    "Animation": "Not a genre so much as total freedom. Animation draws worlds live action can't afford and feelings a camera can't frame.",
    "Comedy": "Built to make you laugh — timing, absurdity, and the long setup paying off. The plot is a stage; the joke is the point.",
    "Crime": "The rules broken, and what breaking them costs. Crime follows heists, syndicates, and detectives down streets where every choice leaves a mark.",
    "Documentary": "The real thing, framed. Documentary finds its drama in what actually happened — and in who gets to tell it.",
    "Drama": "The weight of being human. Drama leans into conflict that's emotional rather than physical — relationships, loss, ambition, and the cost of a single choice.",
    "Fantasy": "Worlds that run on rules that aren't ours. Magic, myth, and invented orders — fantasy asks what changes when the impossible becomes ordinary.",
    "Horror": "Made to unsettle. Horror works the dread before the reveal — the wrongness at the edge of the frame, and the things that don't stay hidden.",
    "Mystery": "A question the viewer is invited to solve. Mystery withholds, plants, and pays off — the pleasure is piecing it together a step behind the detective.",
    "Romance": "The pull between two people, and everything in the way. Romance follows the distance closing — or not — and the feeling that carries the whole story.",
    "Sci-Fi": "What-if, made rigorous. Science fiction extrapolates from technology and its consequences — the future as a lens on the present.",
    "Slice of Life": "The ordinary, paid attention to. Small days, routines, friendships — the quiet texture of being somewhere real.",
    "Sports": "The discipline of getting better. Training, rivalry, and the team that forms around the chase.",
    "Supernatural": "The everyday world with something extra in it. Spirits, powers, and the unexplained walking among the ordinary.",
    "Thriller": "Tension as the engine. Thrillers tighten — a secret, a clock, a pursuer — until sitting still stops being an option."
};

// warm/cool tints behind covers while they load (mirrors GenreApi.tone)
var palette = [ ["#5d4633","#18110c"], ["#33445d","#0c1118"], ["#5b3a64","#170d1b"],
                ["#3f5640","#111b12"], ["#5a3a3f","#160d0b"], ["#3c4a63","#0e121b"] ];
function tone(i) { return palette[i % palette.length]; }

function descFor(name) { return GENRE_DESC[name] || ""; }

function siblings(kind) {
    if (kind === "movie") return MOVIE_GENRES.slice();
    if (kind === "series") return SHOW_GENRES.slice();
    return ANIME_SIBLINGS.slice();
}

// the tab page's mosaic model: [{ name }] — covers come from the page's Top-10 pool.
function mosaicGenres(kind) {
    return siblings(kind).map(function(n) { return { name: n }; });
}

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

function normalizeArtUrl(url) {
    if (!url) return "";
    return String(url)
        .replace("https://images.metahub.space/", "https://live.metahub.space/")
        .replace("/poster/small/", "/poster/medium/")
        .replace("/poster/large/", "/poster/medium/");
}

// ---- anime: live MAL genre ids + counts, cached for the session ----
var animeGenreCache = null;   // [{ mal_id, name, count }]
function ensureAnimeGenres(done) {
    if (animeGenreCache) { done(animeGenreCache); return; }
    requestJson(JIKAN + "/genres/anime", function(j) {
        if (j && j.data) animeGenreCache = j.data;
        done(animeGenreCache || []);
    });
}
function animeGenreEntry(name) {
    var list = animeGenreCache || [];
    for (var i = 0; i < list.length; i++)
        if (list[i].name === name) return list[i];
    return null;
}

function membersLabel(m) { return m >= 1000 ? Math.round(m / 1000) + "K" : String(m || 0); }

// one Jikan anime entry → the MAL-template card (rich: synopsis / score / watchers)
function animeToCard(m, i) {
    var t = tone(i);
    var img = (m.images && m.images.jpg && (m.images.jpg.large_image_url || m.images.jpg.image_url)) || "";
    var year = m.year || ((m.aired && m.aired.prop && m.aired.prop.from) ? m.aired.prop.from.year : null);
    var syn = (m.synopsis || "").replace(/\s*\[Written by MAL Rewrite\]\s*$/, "").replace(/\s+/g, " ").trim();
    if (syn.length > 240) syn = syn.slice(0, 240) + "…";
    var title = m.title_english || m.title;
    return {
        title: title,
        cover: img, c1: t[0], c2: t[1],
        type: m.type || "", year: year, status: m.status || "",
        metaCounts: m.episodes ? m.episodes + " ep" : (m.status === "Currently Airing" ? "airing" : "—"),
        score: (m.score !== null && m.score !== undefined) ? m.score : null,
        members: membersLabel(m.members),
        authors: (m.studios || []).map(function(s) { return s.name; }).slice(0, 2).join(", "),
        genres: (m.genres || []).map(function(g) { return g.name; }).slice(0, 5),
        synopsis: syn,
        item: { id: "mal:" + m.mal_id, type: "series", title: title, cover: img }
    };
}

// one Cinemeta meta → the lean card (poster · title · rank · rating; absent fields don't render)
function cinemetaToCard(kind, meta, i) {
    var t = tone(i);
    var poster = normalizeArtUrl(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : ""));
    var year = meta.releaseInfo ? String(meta.releaseInfo).split(/[–-]/)[0] : null;
    var score = meta.imdbRating ? Number(meta.imdbRating) : null;
    return {
        title: meta.name || "Untitled",
        cover: poster, c1: t[0], c2: t[1],
        type: kind === "movie" ? "Movie" : "Series", year: year, status: "",
        metaCounts: "",
        score: (score !== null && !isNaN(score)) ? score : null,
        members: "",
        authors: "",
        genres: (meta.genres || []).slice(0, 5),
        synopsis: (meta.description || "").replace(/\s+/g, " ").trim().slice(0, 240),
        item: { id: meta.id || "", type: kind, title: meta.name || "", cover: poster }
    };
}

// load a genre page. kind: "movie" | "series" | "anime". sort: "readers" (watchers) | "score".
function loadGenre(kind, name, sort, push) {
    function fail() { push({ count: 0, desc: descFor(name), cards: [], montage: [] }); }
    if (kind === "anime") {
        ensureAnimeGenres(function() {
            var g = animeGenreEntry(name);
            if (!g) { fail(); return; }
            var order = (sort === "score") ? "score&sort=desc" : "popularity&sort=asc";
            var url = JIKAN + "/anime?genres=" + g.mal_id + "&order_by=" + order + "&limit=24&sfw=true";
            requestJson(url, function(j) {
                if (!j || !j.data) { fail(); return; }
                var cards = j.data.map(animeToCard);
                var total = (j.pagination && j.pagination.items && j.pagination.items.total) || g.count || cards.length;
                var montage = cards.slice(0, 7).map(function(c) { return c.cover; }).filter(function(u) { return u; });
                push({ count: total, desc: descFor(name), cards: cards, montage: montage });
            });
        });
        return;
    }
    var type = (kind === "series") ? "series" : "movie";
    var path = "/catalog/" + type + "/top/genre=" + encodeURIComponent(name) + ".json";
    requestJson(CINEMETA_CATALOGS + path, function(j) {
        function build(metas) {
            var cards = metas.slice(0, 24).map(function(m, i) { return cinemetaToCard(kind, m, i); });
            var montage = cards.slice(0, 7).map(function(c) { return c.cover; }).filter(function(u) { return u; });
            // Cinemeta has no genre totals — count 0 keeps the hero's count line hidden.
            push({ count: 0, desc: descFor(name), cards: cards, montage: montage });
        }
        if (j && j.metas) { build(j.metas); return; }
        requestJson(CINEMETA + path, function(j2) {          // fallback host, same contract
            if (j2 && j2.metas) build(j2.metas); else fail();
        });
    });
}

// ---- the Explore index: GenreIndex-shaped groups ----
function tileSwatch(i) { var t = tone(i); return { c1: t[0], c2: t[1] }; }

function classOfAnime(name) {
    if (ANIME_GENRES.indexOf(name) >= 0) return "Genres";
    if (ANIME_EXPLICIT.indexOf(name) >= 0) return "Explicit Genres";
    if (ANIME_DEMOGRAPHICS.indexOf(name) >= 0) return "Demographics";
    return "Themes";
}

function groupSub(name) {
    if (name === "Genres") return "The core shelves";
    if (name === "Explicit Genres") return "Mature — clearly marked";
    if (name === "Themes") return "What it's about";
    if (name === "Demographics") return "Who it was written for";
    return "";
}

// loadGroups(kind, includeExplicit, done) → [{ name, sub, genres: [{ name, count, cover, c1, c2 }] }]
function loadGroups(kind, includeExplicit, done) {
    if (kind === "anime") {
        ensureAnimeGenres(function(list) {
            requestJson(JIKAN + "/top/anime?limit=25&sfw=true", function(top) {
                var pool = (top && top.data ? top.data : []).map(function(m) {
                    return (m.images && m.images.jpg && (m.images.jpg.large_image_url || m.images.jpg.image_url)) || "";
                }).filter(function(u) { return u; });
                var sections = { "Genres": [], "Explicit Genres": [], "Themes": [], "Demographics": [] };
                for (var i = 0; i < list.length; i++) {
                    var cls = classOfAnime(list[i].name);
                    if (cls === "Explicit Genres" && !includeExplicit) continue;
                    var sw = tileSwatch(i);
                    sections[cls].push({ name: list[i].name, count: list[i].count || 0,
                                         cover: pool.length ? pool[i % pool.length] : "",
                                         c1: sw.c1, c2: sw.c2 });
                }
                var order = ["Genres", "Explicit Genres", "Themes", "Demographics"];
                var out = [];
                for (var s = 0; s < order.length; s++)
                    if (sections[order[s]].length)
                        out.push({ name: order[s], sub: groupSub(order[s]), genres: sections[order[s]] });
                done(out);
            });
        });
        return;
    }
    // movie / series: Cinemeta's genre space is flat — one clean section. Covers cycle the
    // lane's Top-10 posters (one catalog call), counts unknown → 0 (tiles hide zero counts).
    var type = (kind === "series") ? "series" : "movie";
    var names = (kind === "series") ? SHOW_GENRES : MOVIE_GENRES;
    requestJson(CINEMETA_CATALOGS + "/catalog/" + type + "/top.json", function(j) {
        var pool = (j && j.metas ? j.metas : []).slice(0, 25).map(function(m) {
            return normalizeArtUrl(m.poster || "");
        }).filter(function(u) { return u; });
        var genres = names.map(function(n, i) {
            var sw = tileSwatch(i);
            return { name: n, count: 0, cover: pool.length ? pool[i % pool.length] : "",
                     c1: sw.c1, c2: sw.c2 };
        });
        done([{ name: "Genres", sub: "The core shelves", genres: genres }]);
    });
}
