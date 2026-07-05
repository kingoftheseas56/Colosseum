// WorldSearch.js — live search adapters for the non-Biblio worlds. Each returns a uniform result
// shape the generic SearchSurface renders: { cover, title, subtitle, data }. `data` is what the host
// routes to the world's detail (manga → a title string for MangaSeries; theatre → the Cinemeta meta).
//   • Theatre → Cinemeta (the Theatre world's own source)
//   • Tankoban → AniList GraphQL (manga) + GetComics tags (western comics)
// Both verified reachable 2026-06-27; GetComics WP REST proven 2026-07-04.
.pragma library
.import "ComicsApi.js" as ComicsApi

function reqJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

// ── Theatre: Cinemeta movie + series search ──
var CINEMETA = "https://v3-cinemeta.strem.io";

function metahubPoster(meta) {
    if (meta.poster) return String(meta.poster).replace("/poster/small/", "/poster/medium/");
    if (meta.id) return "https://live.metahub.space/poster/medium/" + meta.id + "/img";
    return "";
}
function metahubBackdrop(meta) {
    if (meta.background) return meta.background;
    if (meta.id) return "https://live.metahub.space/background/medium/" + meta.id + "/img";
    return "";
}

// one Cinemeta meta → the rich result shape (Harbor-style: kind · year · ★rating, backdrop, synopsis)
function mapTheatre(meta) {
    var kind = meta.type === "series" ? "Series" : "Movie";
    var bits = [kind];
    if (meta.releaseInfo) bits.push(String(meta.releaseInfo));
    if (meta.imdbRating) bits.push("★ " + meta.imdbRating);
    return {
        cover: metahubPoster(meta),
        title: meta.name || meta.title || "Untitled",
        subtitle: kind + (meta.releaseInfo ? "  ·  " + meta.releaseInfo : ""),
        meta: bits.join("   ·   "),
        synopsis: meta.description || "",
        backdrop: metahubBackdrop(meta),
        group: meta.type === "series" ? "Series" : "Movies",
        data: meta
    };
}

// ── Top-match scoring, shared by every lane (Theatre AND Tankoban) ──
// Normalizes case / punctuation / leading articles, then tiers: exact > starts-with >
// whole-word inside > substring. Ties break on the source's OWN relevance rank (each lane's
// index), so a series Cinemeta ranked #1 no longer loses to movie #9 just because the movie
// lane was concatenated first. All-zero scores keep the original order (no fake hero shuffle).
function normTitle(s) {
    s = String(s || "").toLowerCase();
    s = s.replace(/[^a-z0-9 ]+/g, " ").replace(/\s+/g, " ").trim();
    return s.replace(/^(the|a|an) /, "");
}
function scoreTitle(query, title) {
    var q = normTitle(query), t = normTitle(title);
    if (!q.length || !t.length) return 0;
    if (t === q) return 400;
    if (t.indexOf(q) === 0) return 300;
    if ((" " + t + " ").indexOf(" " + q + " ") >= 0) return 200;
    if (t.indexOf(q) >= 0) return 100;
    return 0;
}
function pickTopMatch(query, all) {
    var bestIdx = -1, best = 0;
    for (var i = 0; i < all.length; i++) {
        var sc = scoreTitle(query, all[i].title);
        if (sc <= 0) continue;
        // same-tier ties (e.g. "dune" = the 2021 film AND the 2000 miniseries, both exact)
        // break on IMDb rating first — the popular one is what the user meant — then on the
        // source's own lane rank.
        var rating = parseFloat(all[i].data && all[i].data.imdbRating) || 0;
        var s = sc * 1000 + rating * 10 - (all[i].rank !== undefined ? all[i].rank : i);
        if (s > best) { best = s; bestIdx = i; }
    }
    if (bestIdx > 0) all.unshift(all.splice(bestIdx, 1)[0]);
    return all;
}

function searchTheatre(query, done) {
    if (!query || query.trim().length < 2) { done([]); return; }
    var q = encodeURIComponent(query.trim());
    var out = { movie: [], series: [] }, pending = 2;
    function finish() {
        pending -= 1;
        if (pending > 0) return;
        var mapLane = function(lane) {
            return lane.map(function(m, i) { var r = mapTheatre(m); r.rank = i; return r; });
        };
        var all = pickTopMatch(query, mapLane(out.movie).concat(mapLane(out.series)));
        // Show the list IMMEDIATELY — the old code held every result hostage behind a third,
        // serial /meta/ call for the hero's synopsis. Now the grid paints as soon as both
        // catalogs answer, and the hero enriches in a second done() pass when it lands.
        done(all);
        if (all.length === 0) return;
        var top = all[0], m = top.data;
        reqJson(CINEMETA + "/meta/" + (m.type || "movie") + "/" + m.id + ".json", function(mj) {
            if (!mj || !mj.meta) return;
            var f = mj.meta, kind = (m.type === "series" ? "Series" : "Movie");
            var enriched = {};
            for (var k in top) enriched[k] = top[k];
            if (f.description) enriched.synopsis = f.description;
            var bits = [kind];
            if (f.releaseInfo || m.releaseInfo) bits.push(String(f.releaseInfo || m.releaseInfo));
            if (f.imdbRating) bits.push("★ " + f.imdbRating);
            enriched.meta = bits.join("   ·   ");
            if (f.background && !enriched.backdrop) enriched.backdrop = f.background;
            var all2 = all.slice();
            all2[0] = enriched;
            done(all2);
        });
    }
    reqJson(CINEMETA + "/catalog/movie/top/search=" + q + ".json", function(j) {
        out.movie = (j && j.metas ? j.metas : []).slice(0, 16).map(function(m) { m.type = "movie"; return m; });
        finish();
    });
    reqJson(CINEMETA + "/catalog/series/top/search=" + q + ".json", function(j) {
        out.series = (j && j.metas ? j.metas : []).slice(0, 16).map(function(m) { m.type = "series"; return m; });
        finish();
    });
}

// ── Tankoban: AniList manga search ──
function searchManga(query, done) {
    if (!query || query.trim().length < 2) { done([]); return; }
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done([]); return; }
        try {
            var d = JSON.parse(xhr.responseText);
            var media = d.data.Page.media;
            done(media.map(function(m) {
                var t = m.title.english || m.title.romaji || "Untitled";
                var fmt = m.format ? String(m.format).replace(/_/g, " ") : "Manga";
                return {
                    cover: m.coverImage ? m.coverImage.large : "",
                    title: t,
                    subtitle: fmt,
                    meta: fmt,
                    synopsis: "",
                    backdrop: "",
                    group: "Manga",
                    data: { title: t }
                };
            }));
        } catch (e) { done([]); }
    };
    xhr.open("POST", "https://graphql.anilist.co");
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader("Accept", "application/json");
    var gql = "query($s:String){Page(perPage:24){media(search:$s,type:MANGA,sort:SEARCH_MATCH){title{romaji english} coverImage{large} format}}}";
    xhr.send(JSON.stringify({ query: gql, variables: { s: query.trim() } }));
}

// ── Tankoban: manga (AniList) + western comics (GetComics tags), one surface.
// Western cards carry data.western so the host routes them to the GetComics
// shelf (ComicSeries) instead of the WeebCentral series page. ──
function searchWestern(query, done) {
    ComicsApi.searchSeries(query, function(tags) {
        if (!tags || tags.length === 0) { done([]); return; }
        var out = tags.map(function(t) {
            return {
                cover: "",                       // iTunes poster filled in below
                title: t.title,
                subtitle: t.count + (t.count === 1 ? " release" : " releases"),
                meta: "Western Comics   ·   " + t.count + " releases",
                synopsis: "",
                backdrop: "",
                group: "Western Comics",
                data: { western: true, tag: t.tag, tagId: t.tagId, title: t.title }
            };
        });
        var pending = out.length;
        out.forEach(function(item) {
            ComicsApi.posterFor(item.title + " comic", function(art) {
                item.cover = art;
                pending -= 1;
                if (pending === 0) done(out);
            });
        });
    });
}

function searchTankoban(query, done) {
    if (!query || query.trim().length < 2) { done([]); return; }
    var manga = null, western = null;
    function finish() {
        if (manga === null || western === null) return;
        // Top Match = most title-relevant hit across BOTH lanes, via the shared scorer
        // (normalized, word-boundary-aware, per-lane rank tiebreak — same rule as Theatre).
        var rank = function(lane) { lane.forEach(function(r, i) { r.rank = i; }); return lane; };
        done(pickTopMatch(query, rank(manga).concat(rank(western))));
    }
    searchManga(query, function(items) { manga = items || []; finish(); });
    searchWestern(query, function(items) { western = items || []; finish(); });
}

function searchFor(mode, query, done) {
    if (mode === "Theatre") searchTheatre(query, done);
    else if (mode === "Tankoban") searchTankoban(query, done);
    else done([]);
}

// ── Browse-a-genre (Harbor's empty-state "Try a genre" → inline grid) ──
// AniList + Cinemeta both filter by genre directly, so a chip opens a real popularity-ranked grid.
var MANGA_GENRES = ["Action", "Adventure", "Comedy", "Drama", "Fantasy", "Horror", "Mahou Shoujo",
    "Mecha", "Mystery", "Psychological", "Romance", "Sci-Fi", "Slice of Life", "Sports", "Supernatural", "Thriller"];
var THEATRE_GENRES = ["Action", "Adventure", "Animation", "Comedy", "Crime", "Documentary", "Drama",
    "Family", "Fantasy", "Horror", "Mystery", "Romance", "Sci-Fi", "Thriller"];

function genresFor(mode) { return mode === "Theatre" ? THEATRE_GENRES : MANGA_GENRES; }

// Cinemeta's genre catalog lives on the catalogs host (v3-cinemeta 307-redirects there).
var CINEMETA_CAT = "https://cinemeta-catalogs.strem.io/top";

function browseTheatreGenre(genre, done) {
    var g = encodeURIComponent(genre);
    var out = { movie: [], series: [] }, pending = 2;
    function finish() {
        pending -= 1;
        if (pending > 0) return;
        var all = [], mx = Math.max(out.movie.length, out.series.length);
        for (var i = 0; i < mx; i++) {                     // interleave movies + series
            if (out.movie[i]) all.push(out.movie[i]);
            if (out.series[i]) all.push(out.series[i]);
        }
        done(all);
    }
    reqJson(CINEMETA_CAT + "/catalog/movie/top/genre=" + g + ".json", function(j) {
        out.movie = (j && j.metas ? j.metas : []).slice(0, 30).map(function(m) { m.type = "movie"; return mapTheatre(m); });
        finish();
    });
    reqJson(CINEMETA_CAT + "/catalog/series/top/genre=" + g + ".json", function(j) {
        out.series = (j && j.metas ? j.metas : []).slice(0, 30).map(function(m) { m.type = "series"; return mapTheatre(m); });
        finish();
    });
}

function browseMangaGenre(genre, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done([]); return; }
        try {
            var media = JSON.parse(xhr.responseText).data.Page.media;
            done(media.map(function(m) {
                var t = m.title.english || m.title.romaji || "Untitled";
                var fmt = m.format ? String(m.format).replace(/_/g, " ") : "Manga";
                return { cover: m.coverImage ? m.coverImage.large : "", title: t, subtitle: fmt,
                    meta: fmt, synopsis: "", backdrop: "", group: "Manga", data: { title: t } };
            }));
        } catch (e) { done([]); }
    };
    xhr.open("POST", "https://graphql.anilist.co");
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.setRequestHeader("Accept", "application/json");
    var gql = "query($g:String){Page(perPage:30){media(genre:$g,type:MANGA,sort:POPULARITY_DESC){title{romaji english} coverImage{large} format}}}";
    xhr.send(JSON.stringify({ query: gql, variables: { g: genre } }));
}

function browseGenre(mode, genre, done) {
    if (mode === "Theatre") browseTheatreGenre(genre, done);
    else if (mode === "Tankoban") browseMangaGenre(genre, done);
    else done([]);
}

// Surprise me — a random genre, then a random title from its top results → opens that detail.
function surprise(mode, done) {
    var gs = genresFor(mode);
    var g = gs[Math.floor(Math.random() * gs.length)];
    browseGenre(mode, g, function(items) {
        if (!items || items.length === 0) { done(null); return; }
        done(items[Math.floor(Math.random() * Math.min(items.length, 20))]);
    });
}
