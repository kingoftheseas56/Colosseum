// TheatreApi.js - tiny live catalog adapter for the Colosseum QML prototype.
// Cinemeta is the identity source for movies, series, and anime-shaped series rows.
.pragma library

var CINEMETA = "https://v3-cinemeta.strem.io";

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
    var path = CINEMETA + "/catalog/" + type + "/top";
    if (genre)
        path += "/genre=" + encodeURIComponent(genre);
    requestJson(path + ".json", function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

// Full Cinemeta meta for a detail page (incl. `videos[]` episodes for series).
// type: "movie" | "series"; id: Cinemeta id e.g. "tt15239678". Calls done(meta) or done(null).
function loadMeta(type, id, done) {
    if (!type || !id) { done(null); return; }
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
        blurb: cleanText(meta.description, "Cinemeta catalog entry."),
        cover: normalizeArtUrl(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : "")),
        art: normalizeArtUrl(meta.background || (meta.id ? "https://live.metahub.space/background/medium/" + meta.id + "/img" : "")),
        ghost: meta.type === "series" ? "S" : "T",
        c1: t[0],
        c2: t[1],
        progress: -1
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
        done({
            featured: featured,
            continueItems: rows.movies.slice(0, 2).concat(rows.series.slice(0, 2), rows.anime.slice(0, 1))
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
