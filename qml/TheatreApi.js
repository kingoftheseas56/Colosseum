// TheatreApi.js - tiny live catalog adapter for the Colosseum QML prototype.
// Mirrors the reference stack: Cinemeta for movies/series, Kitsu for anime art/rows.
.pragma library

var CINEMETA = "https://v3-cinemeta.strem.io";
var KITSU = "https://kitsu.io/api/edge";

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

function cinemetaCatalog(type, genre, done) {
    var path = CINEMETA + "/catalog/" + type + "/top";
    if (genre)
        path += "/genre=" + encodeURIComponent(genre);
    requestJson(path + ".json", function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

function kitsuPopular(done) {
    requestJson(KITSU + "/anime?sort=popularityRank&page[limit]=12", function(json) {
        done(json && json.data ? json.data : []);
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
        caption: metaTitle(meta),
        title: metaTitle(meta),
        blurb: cleanText(meta.description, "Cinemeta catalog entry."),
        cover: meta.poster || (meta.id ? "https://images.metahub.space/poster/medium/" + meta.id + "/img" : ""),
        art: meta.background || (meta.id ? "https://images.metahub.space/background/medium/" + meta.id + "/img" : ""),
        ghost: meta.type === "series" ? "S" : "T",
        c1: t[0],
        c2: t[1],
        progress: -1
    };
}

function kitsuTitle(attrs) {
    if (!attrs)
        return "Anime";
    if (attrs.titles && (attrs.titles.en || attrs.titles.en_us))
        return attrs.titles.en || attrs.titles.en_us;
    return attrs.canonicalTitle || (attrs.titles && attrs.titles.en_jp) || "Anime";
}

function mapKitsu(item, index) {
    var attrs = item.attributes || {};
    var t = tone(index + 2);
    var poster = attrs.posterImage || {};
    var cover = attrs.coverImage || {};
    var rating = attrs.averageRating ? Math.round(Number(attrs.averageRating)) + "%" : "";
    return {
        caption: kitsuTitle(attrs),
        title: kitsuTitle(attrs),
        blurb: cleanText(attrs.synopsis || attrs.description, "Kitsu anime catalog entry."),
        cover: poster.large || poster.medium || poster.original || "",
        art: cover.large || cover.original || poster.large || "",
        ghost: "A",
        c1: t[0],
        c2: t[1],
        count: rating
    };
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
        if (pending === 0)
            done(out);
    }

    cinemetaCatalog("movie", "", function(items) {
        out.movies = items.slice(0, 12).map(mapCinemeta);
        finish();
    });
    cinemetaCatalog("series", "", function(items) {
        out.series = items.slice(0, 12).map(mapCinemeta);
        finish();
    });
    kitsuPopular(function(items) {
        out.anime = items.slice(0, 12).map(mapKitsu);
        finish();
    });
}

function loadHome(done) {
    loadTheatre(function(rows) {
        var featured = [];
        if (rows.movies.length > 0) featured.push(rows.movies[0]);
        if (rows.series.length > 0) featured.push(rows.series[0]);
        if (rows.anime.length > 0) featured.push(rows.anime[0]);
        done({
            featured: featured,
            continueItems: rows.movies.slice(0, 2).concat(rows.series.slice(0, 2), rows.anime.slice(0, 1))
        });
    });
}
