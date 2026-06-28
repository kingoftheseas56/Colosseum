// TheatreApi.js - tiny live catalog adapter for the Colosseum QML prototype.
// Cinemeta is the identity source for movies, series, and anime-shaped series rows.
.pragma library

var CINEMETA = "https://v3-cinemeta.strem.io";
var CINEMETA_CATALOGS = "https://cinemeta-catalogs.strem.io/top";
var JIKAN = "https://api.jikan.moe/v4";
var ANIME_KITSU = "https://anime-kitsu.strem.fun";

var palette = [
    ["#5d4633", "#18110c"],
    ["#4c2f2a", "#160d0b"],
    ["#33445d", "#0c1118"],
    ["#3f5640", "#111b12"],
    ["#5b3a64", "#170d1b"],
    ["#3c4a63", "#0e121b"]
];

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    var completed = false;
    function finish(value) {
        if (completed)
            return;
        completed = true;
        done(value);
    }
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE)
            return;
        if (xhr.status < 200 || xhr.status >= 300) {
            finish(null);
            return;
        }
        try {
            finish(JSON.parse(xhr.responseText));
        } catch (e) {
            finish(null);
        }
    };
    xhr.ontimeout = function() { finish(null); };
    xhr.onerror = function() { finish(null); };
    xhr.open("GET", url);
    xhr.timeout = 9000;
    xhr.send();
}

function requestJsonWithFallback(urls, done) {
    var index = 0;
    function next() {
        if (index >= urls.length) {
            done(null);
            return;
        }
        requestJson(urls[index], function(json) {
            if (json) {
                done(json);
                return;
            }
            index += 1;
            next();
        });
    }
    next();
}

function normalizeArtUrl(url) {
    if (!url)
        return "";
    var out = String(url)
        .replace("https://images.metahub.space/", "https://live.metahub.space/")
        .replace("/poster/small/", "/poster/medium/")
        .replace("/poster/large/", "/poster/medium/");
    return out;
}

function cinemetaCatalog(type, genre, done) {
    var path = "/catalog/" + type + "/top";
    if (genre)
        path += "/genre=" + encodeURIComponent(genre);
    var urls = [
        CINEMETA_CATALOGS + path + ".json",
        CINEMETA + path + ".json"
    ];
    requestJsonWithFallback(urls, function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

function jikanQuery(path, params, done) {
    var qs = [];
    params = params || {};
    if (params.sfw === undefined)
        params.sfw = "true";
    for (var key in params)
        qs.push(encodeURIComponent(key) + "=" + encodeURIComponent(params[key]));
    requestJson(JIKAN + path + (qs.length ? "?" + qs.join("&") : ""), function(json) {
        done(json && json.data ? json.data : []);
    });
}

// Full Cinemeta meta for a detail page (incl. `videos[]` episodes for series).
// type: "movie" | "series"; id: Cinemeta id e.g. "tt15239678". Calls done(meta) or done(null).
function loadMeta(type, id, done) {
    if (!type || !id) { done(null); return; }
    if (String(id).match(/^(mal|kitsu|anilist|anidb):/)) {
        var enc = encodeURIComponent(id);
        requestJsonWithFallback([
            ANIME_KITSU + "/meta/series/" + enc + ".json",
            ANIME_KITSU + "/meta/movie/" + enc + ".json"
        ], function(json) {
            done(json && json.meta ? json.meta : null);
        });
        return;
    }
    var sType = (type === "series") ? "series" : "movie";
    requestJson(CINEMETA + "/meta/" + sType + "/" + id + ".json", function(json) {
        done(json && json.meta ? json.meta : null);
    });
}

function tone(index) {
    return palette[index % palette.length];
}

function cleanText(text, fallback) {
    if (!text)
        return fallback;
    var out = String(text).replace(/\s+/g, " ").replace(/\(Source:[^)]+\)/g, "").trim();
    return out.length > 190 ? out.substring(0, 187) + "..." : out;
}

function metaTitle(meta) {
    return meta && (meta.name || meta.title) ? (meta.name || meta.title) : "Untitled";
}

function mapCinemeta(meta, index) {
    var t = tone(index);
    return {
        id: meta.id || "",
        type: meta.type || "movie",
        caption: metaTitle(meta),
        title: metaTitle(meta),
        blurb: cleanText(meta.description, "A featured title."),
        cover: normalizeArtUrl(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : "")),
        art: normalizeArtUrl(meta.background || (meta.id ? "https://live.metahub.space/background/medium/" + meta.id + "/img" : "")),
        ghost: meta.type === "series" ? "S" : "T",
        c1: t[0],
        c2: t[1],
        progress: -1
    };
}

function jikanTitle(meta) {
    return meta.title_english || meta.title || meta.title_japanese || "Unknown";
}

function jikanYear(meta) {
    if (meta.year)
        return String(meta.year);
    if (meta.aired && meta.aired.from)
        return String(meta.aired.from).substring(0, 4);
    return "";
}

function jikanPoster(meta) {
    if (meta.images && meta.images.webp && meta.images.webp.large_image_url)
        return meta.images.webp.large_image_url;
    if (meta.images && meta.images.jpg && meta.images.jpg.large_image_url)
        return meta.images.jpg.large_image_url;
    if (meta.images && meta.images.webp && meta.images.webp.image_url)
        return meta.images.webp.image_url;
    if (meta.images && meta.images.jpg && meta.images.jpg.image_url)
        return meta.images.jpg.image_url;
    return "";
}

function mapJikan(meta, index) {
    var t = tone(index + 2);
    var title = jikanTitle(meta);
    var isMovie = meta.type === "Movie";
    return {
        id: meta.mal_id ? "mal:" + meta.mal_id : "",
        type: isMovie ? "movie" : "series",
        caption: title,
        title: title,
        blurb: cleanText(meta.synopsis, "A featured anime title."),
        cover: jikanPoster(meta),
        art: jikanPoster(meta),
        ghost: "A",
        c1: t[0],
        c2: t[1],
        progress: -1,
        releaseInfo: jikanYear(meta),
        source: "Jikan",
        animeKitsuBase: ANIME_KITSU
    };
}

function uniqueById(items) {
    var seen = {};
    var out = [];
    for (var i = 0; i < items.length; i++) {
        var key = items[i].id || items[i].caption;
        if (!key || seen[key])
            continue;
        seen[key] = true;
        out.push(items[i]);
    }
    return out;
}

function row(title, sub, items, ranked) {
    return {
        title: title,
        sub: sub || "",
        ranked: ranked === true,
        items: items || []
    };
}

function hasBackdrop(item) {
    return item && item.art && item.art.length > 0;
}

function dedupeAgainst(rows, seedItems) {
    var seen = {};
    for (var i = 0; i < seedItems.length; i++) {
        if (seedItems[i].id)
            seen[seedItems[i].id] = true;
    }
    var out = [];
    for (var r = 0; r < rows.length; r++) {
        var fresh = [];
        for (var j = 0; j < rows[r].items.length; j++) {
            var item = rows[r].items[j];
            var key = item.id || item.caption;
            if (key && seen[key])
                continue;
            if (key)
                seen[key] = true;
            fresh.push(item);
        }
        if (fresh.length >= 4)
            out.push(row(rows[r].title, rows[r].sub, fresh, rows[r].ranked));
    }
    return out;
}

function tile(title, ghost, c1, c2, item) {
    return {
        title: title,
        ghost: ghost,
        c1: c1,
        c2: c2,
        item: item || ({})
    };
}

function genreTileSet(rows) {
    var colors = [
        ["#6b3f2d", "#16100d"],
        ["#4f6546", "#11180f"],
        ["#384f78", "#0b101a"],
        ["#7a394c", "#160b10"]
    ];
    var out = [];
    for (var i = 0; i < rows.length && out.length < 4; i++) {
        if (!rows[i].items || rows[i].items.length === 0)
            continue;
        var label = rows[i].title.replace(/^Top\s+/, "");
        out.push(tile(label, label.substring(0, 1), colors[out.length][0], colors[out.length][1], rows[i].items[0]));
    }
    return out;
}

function languageTileSet(rows) {
    var pool = [];
    for (var r = 0; r < rows.length; r++) {
        for (var i = 0; i < rows[r].items.length; i++)
            pool.push(rows[r].items[i]);
    }
    return [
        tile("Japanese Cinema", "JP", "#5d3848", "#130b10", pool[0]),
        tile("Korean Stories", "KR", "#385066", "#0b1016", pool[1]),
        tile("French Picks", "FR", "#63533a", "#15110b", pool[2]),
        tile("Anime", "A", "#4b3a78", "#100c18", pool[3])
    ];
}

function pageBundle(pageKey, rows) {
    var hero = [];
    var topPicks = [];
    var awardTiles = [];
    var dataRows = rows || [];

    if (pageKey === "movies") {
        hero = dataRows.length ? dataRows[0].items.filter(hasBackdrop).slice(0, 5) : [];
        dataRows = dedupeAgainst(dataRows, hero);
    } else if (pageKey === "shows") {
        hero = dataRows.length ? dataRows[0].items.filter(hasBackdrop).slice(0, 6) : [];
        dataRows = dedupeAgainst(dataRows, hero);
    } else if (pageKey === "anime") {
        hero = dataRows.length ? dataRows[0].items.filter(hasBackdrop).slice(0, 5) : [];
        topPicks = dataRows.length > 1 ? dataRows[1].items.slice(0, 18) : [];
        dataRows = dedupeAgainst(dataRows, hero.concat(topPicks));
    } else {
        var topMovies = dataRows.length > 0 ? dataRows[0].items.filter(hasBackdrop).slice(0, 4) : [];
        var topSeries = dataRows.length > 1 ? dataRows[1].items.filter(hasBackdrop).slice(0, 4) : [];
        for (var i = 0; i < Math.max(topMovies.length, topSeries.length) && hero.length < 6; i++) {
            if (topMovies[i]) hero.push(topMovies[i]);
            if (topSeries[i]) hero.push(topSeries[i]);
        }
        awardTiles = dataRows.length > 0 ? dataRows[0].items.slice(0, 8) : [];
        dataRows = dedupeAgainst(dataRows.slice(2), hero.concat(awardTiles));
    }

    return {
        pageKey: pageKey,
        rows: dataRows,
        hero: hero,
        topPicks: topPicks,
        awardTiles: awardTiles,
        genreTiles: pageKey === "discover" ? genreTileSet(dataRows) : [],
        languageTiles: pageKey === "discover" ? languageTileSet(dataRows) : []
    };
}

function catalogFetch(type, genre, limit, done) {
    cinemetaCatalog(type, genre, function(items) {
        done(items.slice(0, limit || 30).map(mapCinemeta));
    });
}

function jikanFetch(path, params, limit, done) {
    jikanQuery(path, params || {}, function(items) {
        done(uniqueById(items.map(mapJikan)).slice(0, limit || 30));
    });
}

function runSpecs(pageKey, specs, done, sequential) {
    var rows = [];
    var pending = specs.length;
    if (pending === 0) {
        done({ pageKey: pageKey, rows: [] });
        return;
    }
    function fetchOne(i) {
        if (i >= specs.length)
            return;
        (function(spec, index) {
            spec.fetch(function(items) {
                if (items && items.length > 0)
                    rows[index] = row(spec.title, spec.sub, items, spec.ranked);
                pending -= 1;
                if (pending === 0) {
                    var out = [];
                    for (var j = 0; j < rows.length; j++)
                        if (rows[j])
                            out.push(rows[j]);
                    done({ pageKey: pageKey, rows: out });
                } else if (sequential) {
                    fetchOne(index + 1);
                }
            });
        })(specs[i], i);
    }
    if (sequential) {
        fetchOne(0);
        return;
    }
    for (var i = 0; i < specs.length; i++) {
        fetchOne(i);
    }
}

function runSpecsProgressive(pageKey, specs, done) {
    var rows = [];
    function publish() {
        var out = [];
        for (var j = 0; j < rows.length; j++)
            if (rows[j])
                out.push(rows[j]);
        done({ pageKey: pageKey, rows: out });
    }
    function fetchOne(index) {
        if (index >= specs.length)
            return;
        var spec = specs[index];
        spec.fetch(function(items) {
            if (items && items.length > 0)
                rows[index] = row(spec.title, spec.sub, items, spec.ranked);
            publish();
            fetchOne(index + 1);
        });
    }
    fetchOne(0);
}

function movieGenreSpecs() {
    var genres = ["Action", "Drama", "Comedy", "Sci-Fi", "Thriller", "Horror", "Romance", "Animation", "Adventure", "Crime", "Mystery", "Fantasy", "Documentary"];
    var specs = [{
        title: "Top Movies",
        sub: "",
        ranked: true,
        fetch: function(done) { catalogFetch("movie", "", 20, done); }
    }];
    for (var i = 0; i < genres.length; i++) {
        (function(genre) {
            specs.push({
                title: "Top " + genre,
                sub: "",
                fetch: function(done) { catalogFetch("movie", genre, 20, done); }
            });
        })(genres[i]);
    }
    return specs;
}

function showGenreSpecs() {
    var genres = ["Drama", "Comedy", "Crime", "Sci-Fi", "Thriller", "Mystery", "Action", "Animation", "Adventure", "Fantasy", "Documentary", "Romance", "Horror"];
    var specs = [{
        title: "Top Series",
        sub: "",
        ranked: true,
        fetch: function(done) { catalogFetch("series", "", 20, done); }
    }];
    for (var i = 0; i < genres.length; i++) {
        (function(genre) {
            specs.push({
                title: "Top " + genre,
                sub: "",
                fetch: function(done) { catalogFetch("series", genre, 20, done); }
            });
        })(genres[i]);
    }
    return specs;
}

function discoverSpecs() {
    return [
        {
            title: "Top Movies",
            sub: "",
            ranked: true,
            fetch: function(done) { catalogFetch("movie", "", 20, done); }
        },
        {
            title: "Top Series",
            sub: "",
            ranked: true,
            fetch: function(done) { catalogFetch("series", "", 20, done); }
        },
        {
            title: "Top Drama",
            sub: "",
            fetch: function(done) { catalogFetch("movie", "Drama", 20, done); }
        },
        {
            title: "Top Comedy",
            sub: "",
            fetch: function(done) { catalogFetch("movie", "Comedy", 20, done); }
        },
        {
            title: "Top Action",
            sub: "",
            fetch: function(done) { catalogFetch("movie", "Action", 20, done); }
        },
        {
            title: "Top Sci-Fi",
            sub: "",
            fetch: function(done) { catalogFetch("movie", "Sci-Fi", 20, done); }
        },
        {
            title: "Top Thriller",
            sub: "",
            fetch: function(done) { catalogFetch("movie", "Thriller", 20, done); }
        }
    ];
}

function animeSpecs() {
    return [
        {
            title: "Airing Now",
            sub: "",
            fetch: function(done) { jikanFetch("/seasons/now", { page: 1 }, 20, done); }
        },
        {
            title: "Top Airing",
            sub: "",
            ranked: true,
            fetch: function(done) { jikanFetch("/top/anime", { filter: "airing", page: 1 }, 20, done); }
        },
        {
            title: "Upcoming Season",
            sub: "",
            fetch: function(done) { jikanFetch("/seasons/upcoming", { page: 1 }, 20, done); }
        },
        {
            title: "Top Anime Series",
            sub: "",
            ranked: true,
            fetch: function(done) { jikanFetch("/top/anime", { type: "tv", page: 1 }, 20, done); }
        },
        {
            title: "Anime Movies",
            sub: "",
            fetch: function(done) { jikanFetch("/top/anime", { type: "movie", page: 1 }, 20, done); }
        },
        {
            title: "Popular Anime",
            sub: "",
            fetch: function(done) { jikanFetch("/top/anime", { filter: "bypopularity", page: 1 }, 20, done); }
        },
        {
            title: "Highest Rated Anime",
            sub: "",
            fetch: function(done) { jikanFetch("/top/anime", { page: 1 }, 20, done); }
        },
        {
            title: "2020s Hits",
            sub: "",
            fetch: function(done) { jikanFetch("/anime", { start_date: "2020-01-01", end_date: "2029-12-31", order_by: "score", sort: "desc", min_score: 7.5, page: 1 }, 20, done); }
        },
        {
            title: "Action & Adventure",
            sub: "",
            fetch: function(done) { jikanFetch("/anime", { genres: 1, order_by: "score", sort: "desc", min_score: 7, page: 1 }, 20, done); }
        },
        {
            title: "Romance",
            sub: "",
            fetch: function(done) { jikanFetch("/anime", { genres: 22, order_by: "score", sort: "desc", min_score: 7, page: 1 }, 20, done); }
        }
    ];
}

function pageTitle(pageKey) {
    if (pageKey === "movies") return "Movies";
    if (pageKey === "shows") return "Shows";
    if (pageKey === "anime") return "Anime";
    return "Discover";
}

function pageSubtitle(pageKey) {
    return "";
}

function pageSourceLabel(pageKey) {
    return "";
}

function loadCatalogPage(pageKey, done) {
    if (pageKey === "movies") {
        runSpecs(pageKey, movieGenreSpecs(), function(result) {
            done(pageBundle(pageKey, result.rows || []));
        });
        return;
    }
    if (pageKey === "shows") {
        runSpecs(pageKey, showGenreSpecs(), function(result) {
            done(pageBundle(pageKey, result.rows || []));
        });
        return;
    }
    if (pageKey === "anime") {
        runSpecsProgressive(pageKey, animeSpecs(), function(result) {
            done(pageBundle(pageKey, result.rows || []));
        });
        return;
    }
    runSpecs("discover", discoverSpecs(), function(result) {
        done(pageBundle("discover", result.rows || []));
    });
}

function loadTheatre(done) {
    var out = {
        featured: [],
        movies: [],
        series: [],
        anime: []
    };
    var pending = 3;
    function finish() {
        pending -= 1;
        if (pending === 0) {
            if (out.movies.length > 0) out.featured.push(out.movies[0]);
            if (out.series.length > 0) out.featured.push(out.series[0]);
            if (out.anime.length > 0) out.featured.push(out.anime[0]);
            done(out);
        }
    }

    cinemetaCatalog("movie", "", function(items) {
        out.movies = items.slice(0, 12).map(mapCinemeta);
        finish();
    });
    cinemetaCatalog("series", "", function(items) {
        out.series = items.slice(0, 12).map(mapCinemeta);
        finish();
    });
    cinemetaCatalog("series", "Anime", function(items) {
        out.anime = items.slice(0, 12).map(mapCinemeta);
        finish();
    });
}

function loadHome(done) {
    loadTheatre(function(rows) {
        var featured = [];
        if (rows.movies.length > 0) featured.push(rows.movies[0]);
        if (rows.series.length > 0) featured.push(rows.series[0]);
        if (rows.anime.length > 0) featured.push(rows.anime[0]);
        // Continue is no longer faked from top rows — it comes from the Progress store now.
        done({
            featured: featured
        });
    });
}

function imageUrlsFromRows(rows) {
    var urls = [];
    function push(u) {
        u = normalizeArtUrl(u);
        if (u && urls.indexOf(u) === -1)
            urls.push(u);
    }
    var groups = [rows.featured || [], rows.movies || [], rows.series || [], rows.anime || []];
    for (var g = 0; g < groups.length; g++) {
        for (var i = 0; i < groups[g].length; i++) {
            push(groups[g][i].cover);
            push(groups[g][i].art);
        }
    }
    return urls;
}
