// McuApi.js - live data for the CINEMATIC universe template (MCU), sourced ENTIRELY from the
// Fandom Marvel Cinematic Universe Wiki via its MediaWiki API. NO key, NO login (the standing law)
// - it just needs a browser User-Agent (Fandom 403s the default one; same lesson as the manga side).
//
// All COPY comes from the Wiki, never written by us (Hemanth's rule):
//   - each Phase page's infobox  -> the ordered FILM list  (data-source="films")
//   - each Phase page's lead <p> -> the phase DESCRIPTION
//   - Cinemeta search by title   -> each film's POSTER (keyless JPEG). Fandom's own images are
//                                   served as WebP, which this Qt build can't decode - so the
//                                   ART comes from Cinemeta while STRUCTURE + COPY stay Wiki.
// Assembled per phase: { phase, saga, description, films:[{title,wikiTitle,poster}], capstone }.
// The capstone = the last film in the phase (the team-up the phase builds to).
.pragma library

var API = "https://marvelcinematicuniverse.fandom.com/api.php";
var UA  = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";
var CINEMETA = "https://v3-cinemeta.strem.io";   // keyless JPEG posters (Fandom images are WebP-only)
function normArt(u) { return u ? String(u).replace("/poster/small/", "/poster/medium/") : ""; }

// Phases 1-6 (skip any that return no films, e.g. an unreleased phase). Saga is fixed lore.
var PHASES = [
    { page: "Phase One",   saga: "The Infinity Saga" },
    { page: "Phase Two",   saga: "The Infinity Saga" },
    { page: "Phase Three", saga: "The Infinity Saga" },
    { page: "Phase Four",  saga: "The Multiverse Saga" },
    { page: "Phase Five",  saga: "The Multiverse Saga" },
    { page: "Phase Six",   saga: "The Multiverse Saga" }
];

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.setRequestHeader("User-Agent", UA);   // Qt's XHR allows this; Fandom needs it
    xhr.send();
}

function tidy(t) {
    t = t.replace(/<[^>]+>/g, " ");
    t = t.replace(/&amp;/g, "&").replace(/&quot;/g, '"').replace(/&#160;/g, " ")
         .replace(/&#39;/g, "'").replace(/&nbsp;/g, " ").replace(/&[a-z]+;/g, " ");
    t = t.replace(/\[\d+\]/g, "");                 // [1] reference markers
    t = t.replace(/\s+([,.;:])/g, "$1");           // space-before-punct from stripped links
    return t.replace(/\s+/g, " ").trim();
}

function displayName(wikiTitle) {
    return wikiTitle.replace(/\s*\((film|TV series|miniseries|special)\)\s*$/i, "").trim();
}

// pull the ordered film list out of a phase page's portable-infobox HTML
function filmsFromInfobox(htmlText) {
    var i = htmlText.indexOf('data-source="films"');
    if (i === -1) return [];
    var end = htmlText.indexOf('data-source="', i + 10);
    var chunk = htmlText.substring(i, end === -1 ? i + 2500 : end);
    var titles = [], m, rx = /title="([^"]+)"/g;
    while ((m = rx.exec(chunk)) !== null) {
        var t = m[1];
        if (titles.indexOf(t) === -1) titles.push(t);
    }
    return titles;
}

// the phase's lead description: first substantial paragraph
function descFromHtml(htmlText) {
    var rx = /<p>([\s\S]*?)<\/p>/g, m;
    while ((m = rx.exec(htmlText)) !== null) {
        var c = tidy(m[1]);
        if (c.length > 60) return c;
    }
    return "";
}

// every film poster, for boot/idle prefetch (disk-cache warming so the page opens instant)
function imageUrls(d) {
    var urls = [];
    if (!d || !d.phases) return urls;
    for (var p = 0; p < d.phases.length; p++)
        for (var f = 0; f < d.phases[p].films.length; f++) {
            var poster = d.phases[p].films[f].poster;
            if (poster && urls.indexOf(poster) === -1) urls.push(poster);
        }
    return urls;
}

function loadMcu(done) {
    var results = new Array(PHASES.length);
    var pending = PHASES.length;

    PHASES.forEach(function(ph, idx) {
        var url = API + "?action=parse&prop=text&format=json&page=" + encodeURIComponent(ph.page);
        requestJson(url, function(json) {
            var data = null;
            if (json && json.parse && json.parse.text && json.parse.text["*"]) {
                var h = json.parse.text["*"];
                var films = filmsFromInfobox(h);
                if (films.length) {
                    data = {
                        phase: ph.page, saga: ph.saga,
                        description: descFromHtml(h),
                        films: films.map(function(t) { return { title: displayName(t), wikiTitle: t, poster: "" }; }),
                        capstone: null
                    };
                }
            }
            results[idx] = data;
            if (--pending === 0) afterPhases();
        });
    });

    function afterPhases() {
        var ordered = results.filter(function(r) { return r; });
        var allFilms = [];
        ordered.forEach(function(p) { p.films.forEach(function(f) { allFilms.push(f); }); });
        if (!allFilms.length) { done({ phases: [] }); return; }

        // POSTERS from Cinemeta (keyless JPEG) matched by title - Fandom's images are WebP-only.
        var remaining = allFilms.length;
        function fin() {
            if (--remaining === 0) {
                ordered.forEach(function(p) { p.capstone = p.films.length ? p.films[p.films.length - 1] : null; });
                done({ phases: ordered });
            }
        }
        allFilms.forEach(function(f) {
            var url = CINEMETA + "/catalog/movie/top/search=" + encodeURIComponent(f.title) + ".json";
            requestJson(url, function(json) {
                if (json && json.metas && json.metas.length) {
                    var meta = json.metas[0];
                    f.poster = normArt(meta.poster || "");
                    f.id = meta.id || "";          // real Cinemeta id (tt…) so the tile routes to Theatre
                    f.type = meta.type || "movie";
                }
                fin();
            });
        });
    }
}
