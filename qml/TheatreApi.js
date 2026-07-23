// TheatreApi.js - tiny live catalog adapter for the Colosseum QML prototype.
// Cinemeta is the identity source for movies, series, and anime-shaped series rows.
// Extensions (spec Phase 3): installed catalog extensions add THEIR shelves to the
// tab pages after the house rows, and answer meta asks the house sources can't.
// A .pragma library can't see context properties, so Main.qml pushes the installed
// list in via setExtensions() at boot and on every registry change.
.pragma library
.import "AddonClient.js" as AddonClient

var CINEMETA = "https://v3-cinemeta.strem.io";
var CINEMETA_CATALOGS = "https://cinemeta-catalogs.strem.io/top";
var JIKAN = "https://api.jikan.moe/v4";
var ANIME_KITSU = "https://anime-kitsu.strem.fun";
var JIKAN_CACHE_TTL_MS = 30 * 60 * 1000;
var jikanCache = {};
var jikanInflight = {};

// installed extensions, pushed in from QML (Main.qml owns the wiring)
var extensionsList = [];
var MAX_EXTENSION_ROWS_PER_TAB = 6;
var EXTENSION_ROW_ITEM_CAP = 24;

function setExtensions(list) {
    extensionsList = list || [];
}

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

function requestJsonCached(url, ttlMs, done) {
    var now = Date.now();
    var hit = jikanCache[url];
    if (hit && now - hit.t < ttlMs) {
        done(hit.value);
        return;
    }
    if (jikanInflight[url]) {
        jikanInflight[url].push(done);
        return;
    }
    jikanInflight[url] = [done];
    requestJson(url, function(json) {
        if (json)
            jikanCache[url] = { t: Date.now(), value: json };
        var waiters = jikanInflight[url] || [];
        delete jikanInflight[url];
        for (var i = 0; i < waiters.length; i++)
            waiters[i](json);
    });
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
    requestJsonCached(JIKAN + path + (qs.length ? "?" + qs.join("&") : ""), JIKAN_CACHE_TTL_MS, function(json) {
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
            var meta = json && json.meta ? json.meta : null;
            // Kitsu addon entries are one-season-per-entry, so an anime series can
            // never grow a season selector from them. When the anime carries an IMDb
            // id, pivot to Cinemeta for the full multi-season episode list (Harbor's
            // path); keep the kitsu meta as the fallback if Cinemeta has nothing.
            if (meta && meta.imdb_id && (meta.type === "series" || type === "series")) {
                requestJson(CINEMETA + "/meta/series/" + meta.imdb_id + ".json", function(cj) {
                    var cm = cj && cj.meta ? cj.meta : null;
                    done(cm && cm.videos && cm.videos.length ? cm : meta);
                });
                return;
            }
            done(meta);
        });
        return;
    }
    var sType = (type === "series") ? "series" : "movie";
    if (String(id).indexOf("tt") !== 0) {
        // an id the house sources don't speak (tmdb:…, an addon's own scheme) —
        // ask the installed extensions that claim it (spec Phase 3)
        AddonClient.loadMetaFromExtensions(extensionsList, type, id, done);
        return;
    }
    requestJson(CINEMETA + "/meta/" + sType + "/" + id + ".json", function(json) {
        if (json && json.meta) { done(json.meta); return; }
        // Cinemeta miss on a tt id — an extension may still know it
        AddonClient.loadMetaFromExtensions(extensionsList, type, id, done);
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
    if (meta.images && meta.images.jpg && meta.images.jpg.large_image_url)
        return meta.images.jpg.large_image_url;
    if (meta.images && meta.images.jpg && meta.images.jpg.image_url)
        return meta.images.jpg.image_url;
    if (meta.images && meta.images.webp && meta.images.webp.large_image_url)
        return meta.images.webp.large_image_url;
    if (meta.images && meta.images.webp && meta.images.webp.image_url)
        return meta.images.webp.image_url;
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

function row(title, sub, items, ranked, discoverPin) {
    var r = {
        title: title,
        sub: sub || "",
        ranked: ranked === true,
        items: items || []
    };
    // extension rows carry a See-all pin into Discover; house rows leave the key
    // ABSENT (undefined) so the delegate's `discoverPin !== undefined` gate reads false
    if (discoverPin)
        r.discoverPin = discoverPin;
    return r;
}

function catalogFetch(type, genre, limit, done) {
    cinemetaCatalog(type, genre, function(items) {
        done(items.slice(0, limit || 30).map(mapCinemeta));
    });
}

// ── THE KITSU RUNG (A5 cross-lane touch, Hemanth-authorized 2026-07-13 while A4 sleeps —
// announced in the haven's agents/chat.md). LAW: Jikan stays the FIRST well; Kitsu answers
// ONLY when Jikan fails or returns empty (MAL refuses Jikan's own servers for hours at a
// time — today's outage blanked the anime rows). Kitsu entries carry "kitsu:<id>" ids, which
// loadMeta already routes through the anime-kitsu addon — the detail door works unchanged.
var KITSU_API = "https://kitsu.io/api/edge";

function mapKitsuAnime(m, index) {
    var a = m.attributes || {};
    var t = tone(index + 2);
    var title = (a.titles && a.titles.en) || a.canonicalTitle || "Unknown";
    return {
        id: m.id ? "kitsu:" + m.id : "",
        type: a.subtype === "movie" ? "movie" : "series",
        caption: title,
        title: title,
        blurb: cleanText(a.synopsis || a.description, "A featured anime title."),
        cover: (a.posterImage && (a.posterImage.large || a.posterImage.medium)) || "",
        art: (a.coverImage && a.coverImage.large)
             || (a.posterImage && (a.posterImage.large || a.posterImage.medium)) || "",
        ghost: "A",
        c1: t[0],
        c2: t[1],
        progress: -1,
        releaseInfo: a.startDate ? String(a.startDate).substring(0, 4) : "",
        source: "Kitsu",
        animeKitsuBase: ANIME_KITSU
    };
}

// the rung answers what both jikanFetch call sites actually ask for — the top currently-
// airing anime by readership; if a future spec asks Jikan something else, this still
// answers airing-top rather than a blank row (never blank > exactly-right-but-dead)
function kitsuAiring(limit, done) {
    var url = KITSU_API + "/anime?filter[status]=current&sort=-userCount&page[limit]="
              + Math.min(20, limit || 10);
    requestJsonCached(url, JIKAN_CACHE_TTL_MS, function(json) {
        var items = (json && json.data) ? json.data : [];
        done(uniqueById(items.map(mapKitsuAnime)).slice(0, limit || 30));
    });
}

function jikanFetch(path, params, limit, done) {
    jikanQuery(path, params || {}, function(items) {
        if (!items || !items.length) { kitsuAiring(limit, done); return; }
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
                    rows[index] = row(spec.title, spec.sub, items, spec.ranked, spec.discoverPin);
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
                rows[index] = row(spec.title, spec.sub, items, spec.ranked, spec.discoverPin);
            publish();
            fetchOne(index + 1);
        });
    }
    fetchOne(0);
}

function movieGenreSpecs() {
    return [{
        title: "Top 10 on Movies",
        sub: "",
        ranked: true,
        fetch: function(done) { catalogFetch("movie", "", 10, done); }
    }];
}

function showGenreSpecs() {
    return [{
        title: "Top 10 on Shows",
        sub: "",
        ranked: true,
        fetch: function(done) { catalogFetch("series", "", 10, done); }
    }];
}

function animeSpecs() {
    return [{
        title: "Top 10 on Anime",
        sub: "",
        ranked: true,
        fetch: function(done) { jikanFetch("/top/anime", { filter: "airing", page: 1 }, 10, done); }
    }];
}

// New shelves from installed catalog extensions (spec Phase 3): one row per
// browsable catalog matching this tab's content type, in installed order —
// capped, and deduped against row titles already on the page.
function extensionSpecs(pageKey, existingTitles) {
    var contentType = pageKey === "shows" ? "series"
                    : pageKey === "anime" ? "anime" : "movie";
    var specs = AddonClient.catalogSpecs(extensionsList, contentType);
    var out = [];
    var seen = {};
    for (var i = 0; i < existingTitles.length; i++)
        seen[String(existingTitles[i]).toLowerCase()] = true;
    for (var j = 0; j < specs.length && out.length < MAX_EXTENSION_ROWS_PER_TAB; j++) {
        (function(spec) {
            var key = (spec.extName + " " + spec.title).toLowerCase();
            if (seen[key]) return;
            seen[key] = true;
            out.push({
                title: spec.title,
                sub: "via " + spec.extName,
                ranked: false,
                discoverPin: { transportUrl: spec.transportUrl, type: contentType,
                               catalogId: spec.catalogId, addonName: spec.extName },
                fetch: function(done) {
                    AddonClient.fetchCatalog(spec, function(metas) {
                        done(metas.slice(0, EXTENSION_ROW_ITEM_CAP).map(mapCinemeta));
                    });
                }
            });
        })(specs[j]);
    }
    return out;
}

function withExtensionSpecs(pageKey, baseSpecs) {
    var titles = [];
    for (var i = 0; i < baseSpecs.length; i++)
        titles.push(baseSpecs[i].title);
    return baseSpecs.concat(extensionSpecs(pageKey, titles));
}

function pageTitle(pageKey) {
    if (pageKey === "movies") return "Movies";
    if (pageKey === "shows") return "Shows";
    if (pageKey === "anime") return "Anime";
    return "Movies";
}

function pageSubtitle(pageKey) {
    return "";
}

function pageSourceLabel(pageKey) {
    return "";
}

function loadCatalogPage(pageKey, done) {
    if (pageKey === "shows") {
        runSpecs(pageKey, withExtensionSpecs(pageKey, showGenreSpecs()), function(result) {
            done({ pageKey: pageKey, rows: result.rows || [] });
        });
        return;
    }
    if (pageKey === "anime") {
        runSpecsProgressive(pageKey, withExtensionSpecs(pageKey, animeSpecs()), function(result) {
            done({ pageKey: pageKey, rows: result.rows || [] });
        });
        return;
    }
    runSpecs("movies", withExtensionSpecs("movies", movieGenreSpecs()), function(result) {
        done({ pageKey: "movies", rows: result.rows || [] });
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

// ---- AF2 cast lane -------------------------------------------------------
// Anime cast comes from AniList (face art + VAs); everything else uses the
// name-only `cast` field already in the Cinemeta meta. The discriminator is
// the ORIGINAL requested id (anime ids pivot to tt… after the kitsu→imdb hop).

function postJson(url, body, done) {
    var xhr = new XMLHttpRequest()
    xhr.open("POST", url)
    xhr.setRequestHeader("Content-Type", "application/json")
    xhr.timeout = 9000
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return }
        try { done(JSON.parse(xhr.responseText)) } catch (e) { done(null) }
    }
    xhr.ontimeout = function() { done(null) }
    xhr.onerror = function() { done(null) }
    xhr.send(JSON.stringify(body))
}

function animeIdFor(requestedId) {
    var m = String(requestedId || "").match(/^(mal|anilist):(\d+)$/)
    return m ? { "site": m[1], "id": parseInt(m[2]) } : null
}

// done({cast: [{name, role, image}], studio, source}) — or done(null) to fall
// back to Cinemeta names. `name` = the voice actor (mock anatomy), `role` = the
// character; image = character art.
function loadAnimeCast(requestedId, done) {
    var ref = animeIdFor(requestedId)
    if (!ref) { done(null); return }
    var filter = ref.site === "mal" ? ("idMal:" + ref.id) : ("id:" + ref.id)
    var q = "query{Media(" + filter + ",type:ANIME){source(version:3)"
        + " studios(isMain:true){nodes{name}}"
        + " characters(sort:ROLE,perPage:12){edges{node{name{full} image{large}}"
        + " voiceActors(language:JAPANESE){name{full}}}}}}"
    postJson("https://graphql.anilist.co", { "query": q }, function(json) {
        var media = json && json.data && json.data.Media
        if (!media) { done(null); return }
        var cast = []
        var edges = (media.characters && media.characters.edges) || []
        for (var i = 0; i < edges.length; i++) {
            var ch = edges[i].node || {}
            var va = (edges[i].voiceActors && edges[i].voiceActors[0]) || null
            cast.push({ "name": va && va.name ? va.name.full : (ch.name ? ch.name.full : ""),
                        "role": ch.name ? ch.name.full : "",
                        "image": ch.image ? (ch.image.large || "") : "" })
        }
        var studios = (media.studios && media.studios.nodes) || []
        var src = media.source ? String(media.source).replace(/_/g, " ").toLowerCase() : ""
        done({ "cast": cast,
               "studio": studios.length ? studios[0].name : "",
               "source": src ? src.charAt(0).toUpperCase() + src.slice(1) : "" })
    })
}

// ---- AF2 More Like This --------------------------------------------------
// Same-genre from OUR catalogs, never a recommendations API. Live-action →
// Cinemeta catalog; anime → the baked MAL DB (malCatalog is PASSED IN by the
// page — .pragma libraries can't see context properties). Excludes self.
// done([{id, type, title, cover}]) — at most 12.
function moreLikeThis(mediaType, requestedId, resolvedId, firstGenre, malCatalog, done) {
    if (!firstGenre) { done([]); return }
    var selfIds = {}
    selfIds[String(requestedId || "")] = true
    selfIds[String(resolvedId || "")] = true
    if (animeIdFor(requestedId) || String(requestedId || "").match(/^(kitsu|anidb):/)) {
        if (!malCatalog || !malCatalog.ready()) { done([]); return }
        var rows = malCatalog.genreEntries("anime", firstGenre, "members", 13) || []
        var out = []
        for (var i = 0; i < rows.length && out.length < 12; i++) {
            var r = rows[i]
            var rid = "mal:" + r.mal_id
            if (selfIds[rid]) continue
            out.push({ "id": rid, "type": "series",
                       "title": r.title_english || r.title || "",
                       "cover": (r.images && r.images.jpg && r.images.jpg.large_image_url) || "" })
        }
        done(out)
        return
    }
    catalogFetch(mediaType, firstGenre, 13, function(cards) {
        var out = []
        for (var i = 0; i < (cards || []).length && out.length < 12; i++) {
            var c = cards[i]
            if (selfIds[String(c.id)]) continue
            out.push({ "id": c.id, "type": c.type, "title": c.title || c.caption || "", "cover": c.cover || "" })
        }
        done(out)
    })
}
