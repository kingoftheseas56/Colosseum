// TheatreApi.js - tiny live catalog adapter for the Colosseum QML prototype.
// Cinemeta is the identity source for movies, series, and anime-shaped series rows.
// Extensions (spec Phase 3): installed catalog extensions add THEIR shelves to the
// tab pages after the house rows, and answer meta asks the house sources can't.
// A .pragma library can't see context properties, so Main.qml pushes the installed
// list in via setExtensions() at boot and on every registry change.
.pragma library
.import "AddonClient.js" as AddonClient
.import "TheatreCatalogRules.js" as Rules

var CINEMETA = "https://v3-cinemeta.strem.io";
var CINEMETA_CATALOGS = "https://cinemeta-catalogs.strem.io/top";
var JIKAN = "https://api.jikan.moe/v4";
var ANIME_KITSU = "https://anime-kitsu.strem.fun";
var JIKAN_CACHE_TTL_MS = 30 * 60 * 1000;
var jikanCache = {};
var jikanInflight = {};

// ── Test-only transport seam. When set, EVERY requestJson routes through the adapter
// (url, done) instead of a live XHR, so the offscreen row harness feeds deterministic
// Cinemeta top/genre/full-meta fixtures. Production leaves it null.
var requestAdapter = null;
function setRequestAdapter(fn) { requestAdapter = fn || null; }
function resetRequestAdapter() { requestAdapter = null; }
function noopCancel() {}
// test-only: drop the live (Jikan/Kitsu) + full-meta caches so harness scenarios don't bleed.
function resetLiveCaches() {
    jikanCache = {}; jikanInflight = {};
}

// Deep-catalogue preview tuning (spec §12).
var PREVIEW_ROW_CAP = 20;

// installed extensions, pushed in from QML (Main.qml owns the wiring)
var extensionsList = [];
var MAX_EXTENSION_ROWS_PER_TAB = 6;
var EXTENSION_ROW_ITEM_CAP = 24;

function setExtensions(list) {
    extensionsList = list || [];
}

// Task 9: global Explicit Content preference. The page-load paths already thread
// options.showExplicit + explicitFilter; the boot-time marquee rows (loadTheatre /
// loadHome) and the airing-anime top-10 row don't take an options object, so they
// read this module-level flag instead. Set by Main.qml at boot + on preference
// change. Sexually-explicit ONLY — Berserk/GoT/Ecchi/Mature/TV-MA stay visible.
var showExplicitFlag = false;
function setShowExplicit(v) { showExplicitFlag = v === true; }

var palette = [
    ["#5d4633", "#18110c"],
    ["#4c2f2a", "#160d0b"],
    ["#33445d", "#0c1118"],
    ["#3f5640", "#111b12"],
    ["#5b3a64", "#170d1b"],
    ["#3c4a63", "#0e121b"]
];

function requestJson(url, done) {
    if (requestAdapter) {
        var injected = requestAdapter(url, done);
        return (typeof injected === "function") ? injected : noopCancel;
    }
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
    try {
        xhr.open("GET", url);
        xhr.timeout = 9000;
        xhr.send();
    } catch (e) {
        finish(null);
    }
    var xhrCancel = function() {
        if (completed)
            return;
        completed = true;
        try { xhr.abort(); } catch (e) { /* already closed */ }
    };
    return xhrCancel;
}

function requestJsonWithFallback(urls, done) {
    var index = 0;
    var cancelled = false;
    var finished = false;
    var activeCancel = noopCancel;
    function next() {
        if (cancelled || finished) return;
        if (index >= urls.length) {
            finished = true;
            done(null);
            return;
        }
        var slot = index;
        var handle = requestJson(urls[index], function(json) {
            if (cancelled || finished) return;
            if (json) {
                finished = true;
                done(json);
                return;
            }
            index += 1;
            next();
        });
        // A deterministic adapter may resolve synchronously and start the fallback before
        // the first request returns. Only the handle for the still-current slot may become
        // active; otherwise a hidden page could cancel the already-finished first request
        // while the fallback remains in flight.
        if (!cancelled && slot === index)
            activeCancel = handle;
        else if (cancelled && typeof handle === "function")
            handle();
    }
    next();
    return function() {
        if (cancelled) return;
        cancelled = true;
        activeCancel();
    };
}

function requestJsonCached(url, ttlMs, done) {
    var now = Date.now();
    var hit = jikanCache[url];
    if (hit && now - hit.t < ttlMs) {
        done(hit.value);
        return noopCancel;
    }
    if (jikanInflight[url]) {
        var shared = jikanInflight[url];
        var waiter = { done: done, cancelled: false };
        shared.waiters.push(waiter);
        return function() {
            if (waiter.cancelled) return;
            waiter.cancelled = true;
            for (var wi = shared.waiters.length - 1; wi >= 0; wi--)
                if (shared.waiters[wi] === waiter) shared.waiters.splice(wi, 1);
            if (!shared.waiters.length && jikanInflight[url] === shared) {
                shared.cancel();
                delete jikanInflight[url];
            }
        };
    }
    var entry = { waiters: [{ done: done, cancelled: false }], cancel: noopCancel };
    jikanInflight[url] = entry;
    entry.cancel = requestJson(url, function(json) {
        if (jikanInflight[url] !== entry) return;
        if (json)
            jikanCache[url] = { t: Date.now(), value: json };
        var waiters = entry.waiters.slice();
        delete jikanInflight[url];
        for (var i = 0; i < waiters.length; i++)
            if (!waiters[i].cancelled) waiters[i].done(json);
    });
    return function() {
        var first = entry.waiters[0];
        if (!first || first.cancelled) return;
        first.cancelled = true;
        entry.waiters.shift();
        if (!entry.waiters.length && jikanInflight[url] === entry) {
            entry.cancel();
            delete jikanInflight[url];
        }
    };
}

function normalizeArtUrl(url) {
    if (!url)
        return "";
    // Normalize the Metahub host AND force the `small` poster size. small is the ONLY size metahub
    // reliably serves (medium 404s for many long-tail titles, e.g. tt2431250) and, measured
    // 2026-08-02, small is 300x450 — already sharp at every catalogue display size once decode is
    // 2x-capped, while medium (500x750) is 2-3x the bytes for no visible gain and a ~2.1s dead wait
    // on the missing-medium tail. Forcing small here keeps EVERY cover consumer fast (cards via
    // PosterSourcePolicy, and the genre mosaic which reads item.cover directly). The polish arc's
    // brief medium-first experiment was a net load-speed loss and is retired. (Hemanth, 2026-08-02.)
    var out = String(url)
        .replace("https://images.metahub.space/", "https://live.metahub.space/")
        .replace("/poster/medium/", "/poster/small/")
        .replace("/poster/large/", "/poster/small/");
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
    return requestJsonWithFallback(urls, function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

// Explicit manual Identify fallback only. This is intentionally separate from every shelf
// loader: no automatic identity path calls it. The caller decides when the offline IMDb list
// missed and may inject requestAdapter in deterministic tests.
function searchTitle(query, done) {
    var text = String(query || "").trim();
    if (!text) { done([]); return; }
    function searchKind(type, next) {
        var suffix = ".json?search=" + encodeURIComponent(text);
        requestJsonWithFallback([
            CINEMETA_CATALOGS + "/catalog/" + type + "/top" + suffix,
            CINEMETA + "/catalog/" + type + "/top" + suffix
        ], function(json) {
            next(json && json.metas ? json.metas : []);
        });
    }
    searchKind("movie", function(movies) {
        if (movies.length) { done(movies); return; }
        searchKind("series", done);
    });
}

function jikanQuery(path, params, done) {
    var qs = [];
    params = params || {};
    // Task 9: when no caller pins sfw, derive it from the global preference. sfw=true
    // (the prior default) keeps explicit entries out; sfw=false admits them.
    if (params.sfw === undefined)
        params.sfw = showExplicitFlag ? "false" : "true";
    for (var key in params)
        qs.push(encodeURIComponent(key) + "=" + encodeURIComponent(params[key]));
    return requestJsonCached(JIKAN + path + (qs.length ? "?" + qs.join("&") : ""), JIKAN_CACHE_TTL_MS, function(json) {
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

// distinct real seasons (season > 0) present in a Cinemeta meta's videos[]
function seasonCount(videos) {
    if (!videos || !videos.length) return 0;
    var seen = {};
    for (var i = 0; i < videos.length; i++) {
        var s = videos[i].season;
        if (s && s > 0) seen[s] = true;
    }
    var n = 0;
    for (var k in seen) n++;
    return n;
}

function mapCinemeta(meta, index) {
    var t = tone(index);
    var videos = meta.videos || [];
    return {
        id: meta.id || meta.imdb_id || "",
        type: meta.type || "movie",
        // Keyless TMDB identity: Cinemeta ships `moviedb_id`, which is a TMDB id. Preserved
        // as generic title identity for source extensions (NoTorrent accepts tmdb ids).
        // Absent stays 0 — an unknown id is never guessed.
        tmdbId: Number(meta.moviedb_id || meta.tmdbId || 0),
        caption: metaTitle(meta),
        title: metaTitle(meta),
        blurb: cleanText(meta.description, "A featured title."),
        cover: normalizeArtUrl(meta.poster || (meta.id ? "https://live.metahub.space/poster/medium/" + meta.id + "/img" : "")),
        art: normalizeArtUrl(meta.background || (meta.id ? "https://live.metahub.space/background/medium/" + meta.id + "/img" : "")),
        ghost: meta.type === "series" ? "S" : "T",
        c1: t[0],
        c2: t[1],
        progress: -1,
        // Factual fields retained for the deep catalogue's ranking/filtering. NEVER synthesised:
        // an absent field stays empty so a fact-dependent shelf excludes the item (spec §6.1).
        imdbRating: meta.imdbRating || "",
        releaseInfo: meta.releaseInfo || (meta.year ? String(meta.year) : ""),
        runtime: meta.runtime || "",
        genres: meta.genres || meta.genre || [],
        country: meta.country || "",
        status: meta.status || "",
        popularity: (typeof meta.popularity === "number") ? meta.popularity : undefined,
        behaviorHints: meta.behaviorHints || ({}),
        seasonCount: seasonCount(videos),
        videosKnown: meta.videos !== undefined
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
    return requestJsonCached(url, JIKAN_CACHE_TTL_MS, function(json) {
        var items = (json && json.data) ? json.data : [];
        done(uniqueById(items.map(mapKitsuAnime)).slice(0, limit || 30));
    });
}

function jikanFetch(path, params, limit, done) {
    var cancelled = false;
    var activeCancel = null;
    var initialCancel = jikanQuery(path, params || {}, function(items) {
        if (cancelled) return;
        if (!items || !items.length) {
            activeCancel = kitsuAiring(limit, function(fallback) {
                if (!cancelled) done(fallback);
            });
            return;
        }
        done(uniqueById(items.map(mapJikan)).slice(0, limit || 30));
    });
    if (activeCancel === null)
        activeCancel = initialCancel;
    return function() {
        if (cancelled) return;
        cancelled = true;
        activeCancel();
    };
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

// ═══ Deep catalogue engine (spec 2026-08-01) ══════════════════════════════════════════════
// loadCatalogPage(pageKey, options, push): progressive. options = { malCatalog, showExplicit,
// generation, explicitFilter(item,showExplicit)->bool, nowMs }. push({ pageKey, generation, rows,
// loading, error }) may fire many times as the pool grows and enrichment lands. Every push echoes
// the generation so the page can ignore stale callbacks. Backward-compatible with (pageKey, done).

function cinemetaCatalogPaged(type, genre, skip, done) {
    var path = "/catalog/" + type + "/top";
    if (genre) path += "/genre=" + encodeURIComponent(genre);
    if (skip) path += "/skip=" + skip;
    var urls = [ CINEMETA_CATALOGS + path + ".json", CINEMETA + path + ".json" ];
    return requestJsonWithFallback(urls, function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

// ── IMDb index integration (spec 2026-08-02) ───────────────────────────────────────────
// The baked ImdbCatalog answers every offline-answerable Movies/Shows shelf; live Cinemeta
// rows (Top 10 / recently-released / recently-premiered / currently-airing) are facts-filtered
// through the index so anime (lives in its own tab) and shovelware (below the vote floor) drop.

function posterFor(tt)      { return "https://live.metahub.space/poster/small/" + tt + "/img"; }
function backgroundFor(tt)  { return "https://live.metahub.space/background/medium/" + tt + "/img"; }

function mapImdb(row, index) {
    var t = tone(index);
    return {
        id: row.tt,
        type: row.type === "movie" ? "movie" : "series",
        caption: row.title, title: row.title,
        blurb: "A featured title.",
        cover: posterFor(row.tt), art: backgroundFor(row.tt),
        ghost: row.type === "movie" ? "T" : "S",
        c1: t[0], c2: t[1], progress: -1,
        imdbRating: row.rating > 0 ? String(row.rating.toFixed ? row.rating.toFixed(1) : row.rating) : "",
        releaseInfo: row.year > 0 ? String(row.year) : "",
        runtime: row.runtimeMin > 0 ? (row.runtimeMin + " min") : "",
        genres: row.genres || [], votes: row.votes || 0,
        origLang: row.origLang || "", source: "IMDb"
    };
}

// merged + deduped index rows for one recipe (imdbGenreAny fans out)
function imdbRowsFor(imdb, recipe, offset, limit) {
    var queries = Rules.indexQueriesFor(recipe);
    if (!queries.length) return [];
    if (queries.length === 1) return imdb.titleCatalog(queries[0], offset, limit) || [];
    var merged = [], seen = {};
    for (var i = 0; i < queries.length; i++) {
        var part = imdb.titleCatalog(queries[i], offset, limit) || [];
        for (var j = 0; j < part.length; j++)
            if (!seen[part[j].tt]) { seen[part[j].tt] = true; merged.push(part[j]); }
    }
    merged.sort(function(a, b) { return (b.votes || 0) - (a.votes || 0); });
    return merged.slice(0, limit);
}

// drop live-row items the index disqualifies: anime always; shovelware on recent shelves.
function filterLiveItems(imdb, items, dropLowVotes) {
    if (!imdb || !imdb.ready || !imdb.ready()) return items;
    var ids = items.map(function(it) { return it.id; })
                   .filter(function(id) { return String(id).indexOf("tt") === 0; });
    var facts = imdb.titleFacts(ids) || {};
    return items.filter(function(it) {
        var f = facts[it.id];
        if (!f) return true;                          // unknown to the index -> keep
        if (f.isAnime) return false;                  // anime lives in the Anime tab
        if (dropLowVotes && f.votes < Rules.THRESHOLDS.RECENT_VOTE_FLOOR) return false;
        return true;
    });
}

function rowFromDef(def, items) {
    return {
        key: def.key,
        title: def.title,
        pageKey: def.pageKey,
        ranked: def.ranked === true,
        rotating: def.rotating === true,
        sourceKind: def.sourceKind || "house",
        sourceLabel: def.sourceLabel || "Colosseum",
        placement: def.placement,
        items: items,
        seeAllPin: { pageKey: def.pageKey, sourceKind: "house", rowKey: def.key, title: def.title }
    };
}

function loadMoviesShowsDeep(pageKey, options, push) {
    var type = pageKey === "shows" ? "series" : "movie";
    var generation = options.generation || 0;
    var showExplicit = options.showExplicit === true;
    var explicitFilter = options.explicitFilter || null;
    var imdb = options.imdbCatalog || null;
    var now = options.nowMs || Date.now();

    var defs = Rules.defaultRows(pageKey);
    if (pageKey === "movies") defs = defs.concat(Rules.dailyRows(now, 6));
    defs.sort(function(a, b) { return a.placement - b.placement; });

    var rowData = {};          // key -> items
    var extMainRows = [], extExtensionRows = [];
    var liveDone = false, extDone = false;
    var cancelled = false;
    var cancels = [];
    function keep(items) {
        return explicitFilter
            ? items.filter(function(it) { return explicitFilter(it, showExplicit); }) : items;
    }
    function publish() {
        if (cancelled) return;
        var houseRows = [];
        for (var i = 0; i < defs.length; i++) {
            var def = defs[i];
            var items = keep(rowData[def.key] || []);
            var cap = def.recipe.kind === "top" ? (def.recipe.limit || 10) : PREVIEW_ROW_CAP;
            if (items.length > 0) houseRows.push(rowFromDef(def, items.slice(0, cap)));
        }
        var merged = houseRows.concat(extMainRows);
        merged.sort(function(a, b) { return (a.placement || 0) - (b.placement || 0); });
        push({ pageKey: pageKey, generation: generation,
               rows: merged.concat(extExtensionRows),
               loading: !(liveDone && extDone), error: "" });
    }

    // phase 1 — the index paints every offline shelf synchronously
    if (imdb && imdb.ready && imdb.ready()) {
        for (var i = 0; i < defs.length; i++) {
            var recipe = defs[i].recipe;
            if (Rules.indexQueriesFor(recipe).length === 0) continue;   // live recipe
            var rows = imdbRowsFor(imdb, recipe, 0, PREVIEW_ROW_CAP);
            if (rows.length) rowData[defs[i].key] = rows.map(mapImdb);
        }
    }
    publish();

    // phase 2 — extensions load in parallel (unchanged contract)
    var extensionCancel = loadExtensionRows(pageKey, type, defs,
        { showExplicit: showExplicit, explicitFilter: explicitFilter },
        function() { publish(); },
        function(main, ext) { if (cancelled) return; extMainRows = main; extExtensionRows = ext; extDone = true; publish(); });
    cancels.push(extensionCancel);

    // phase 3 — live rows from Cinemeta, facts-filtered through the index
    var liveCancel = cinemetaCatalog(type, "", function(metas) {
        if (cancelled) return;
        var mapped = (metas || []).map(mapCinemeta);
        var clean = filterLiveItems(imdb, mapped, false);
        rowData["top-10"] = clean;
        var recentKey = pageKey === "shows" ? "recently-premiered" : "recently-released";
        rowData[recentKey] = Rules.rankItems({ kind: "recent" },
                                             filterLiveItems(imdb, mapped, true), now);
        if (pageKey === "shows")
            rowData["currently-airing"] = clean.filter(function(it) {
                return String(it.status || "") === "Continuing";
            });
        liveDone = true;
        publish();
    });
    cancels.push(liveCancel);
    return function() {
        if (cancelled) return;
        cancelled = true;
        for (var i = 0; i < cancels.length; i++)
            if (typeof cancels[i] === "function") cancels[i]();
    };
}

// ── Deep Anime ladder (spec §6.2) ───────────────────────────────────────────────────────
// Local-first: the bundled MAL catalogue paints every offline-answerable shelf immediately;
// the live keyless ladder (Jikan → Kitsu) then refreshes the "hot" shelves. When both live
// sources fail the bundled rows stay visible — nothing blanks. AniList account data is never
// introduced; Trending is OMITTED (not falsified) because no keyless trend signal exists.

// recipe -> a MalCatalog.animeCatalog query (only defined keys; undefined would break the
// strict native allowlist). Returns null for recipes with no single offline query.
function animeQueryFor(recipe) {
    var q = {};
    function put(k, v) { if (v !== undefined && v !== null && v !== "") q[k] = v; }
    switch (recipe.kind) {
    case "top":         put("order", "members"); return q;
    case "animeOrder":  put("order", recipe.order || "members"); put("voteFloor", recipe.voteFloor); return q;
    case "animeStatus": put("status", recipe.status); put("order", recipe.order || "members"); put("voteFloor", recipe.voteFloor); return q;
    case "animeType":   put("type", recipe.type); put("order", recipe.order || "score"); put("voteFloor", recipe.voteFloor); return q;
    case "animeGems":   put("order", "score"); put("voteFloor", recipe.voteFloor); put("membersMin", recipe.membersMin); put("membersMax", recipe.membersMax); return q;
    case "animeDecade": put("order", "members"); put("yearFrom", recipe.from); put("yearTo", recipe.to); return q;
    case "animeTag":    put("order", "members"); put("tag", recipe.tag); return q;
    default:            return null;   // trending, animeTagAny (handled separately)
    }
}

function dedupeMalRows(rows) {
    var seen = {}, out = [];
    for (var i = 0; i < rows.length; i++) {
        var id = rows[i].mal_id;
        if (id === undefined || seen[id]) continue;
        seen[id] = true; out.push(rows[i]);
    }
    return out;
}

// query the bundled catalogue for one recipe (animeTagAny fans out over its tags + dedupes)
function malRowsFor(mal, recipe, offset, limit) {
    if (recipe.kind === "animeTagAny") {
        var merged = [];
        for (var t = 0; t < (recipe.tags || []).length; t++)
            merged = merged.concat(mal.animeCatalog({ order: "members", tag: recipe.tags[t] }, offset, limit) || []);
        return dedupeMalRows(merged).slice(0, limit);
    }
    var q = animeQueryFor(recipe);
    if (!q) return [];
    return mal.animeCatalog(q, offset, limit) || [];
}

// the shelves a keyless live source can genuinely refresh (Jikan route → Kitsu fallback via
// jikanFetch). Genre/tag/decade/gems stay bundled-only — that is exactly why the DB is baked.
var LIVE_ANIME = {
    "top-10":          { path: "/top/anime", params: { filter: "bypopularity" } },
    "airing-now":      { path: "/seasons/now", params: {} },
    "top-airing":      { path: "/top/anime", params: { filter: "airing" } },
    "upcoming-season": { path: "/seasons/upcoming", params: {} },
    "most-popular":    { path: "/top/anime", params: { filter: "bypopularity" } },
    "top-rated":       { path: "/top/anime", params: {} }
};

function refreshAnimeLive(defs, rowData, onRefresh, onDone) {
    var keys = [];
    for (var i = 0; i < defs.length; i++)
        if (LIVE_ANIME[defs[i].key]) keys.push(defs[i].key);
    if (!keys.length) { onDone(); return function() {}; }
    var pending = keys.length;
    var cancelled = false;
    var cancels = [];
    for (var k = 0; k < keys.length; k++) {
        (function(key) {
            var spec = LIVE_ANIME[key];
            var handle = jikanFetch(spec.path, spec.params, PREVIEW_ROW_CAP, function(items) {
                if (cancelled) return;
                // jikanFetch already falls back to Kitsu; only overwrite when a live source
                // actually produced items, so a total live failure leaves the bundled row intact.
                if (items && items.length) rowData[key] = items;
                onRefresh();
                pending -= 1;
                if (pending === 0) onDone();
            });
            cancels.push(handle);
        })(keys[k]);
    }
    return function() {
        if (cancelled) return;
        cancelled = true;
        for (var i = 0; i < cancels.length; i++)
            if (typeof cancels[i] === "function") cancels[i]();
    };
}

function loadAnimePageDeep(options, push) {
    var generation = options.generation || 0;
    var showExplicit = options.showExplicit === true;
    var explicitFilter = options.explicitFilter || null;
    var mal = options.malCatalog || null;

    var defs = Rules.defaultRows("anime");
    defs.sort(function(a, b) { return a.placement - b.placement; });

    var rowData = {};   // key -> mapped card items (best source so far)
    var cancelled = false;
    function keep(items) {
        if (!explicitFilter) return items;
        return items.filter(function(it) { return explicitFilter(it, showExplicit); });
    }
    function publish(loading) {
        if (cancelled) return;
        var rows = [];
        for (var i = 0; i < defs.length; i++) {
            var def = defs[i];
            var items = keep(rowData[def.key] || []);
            var cap = (def.recipe.kind === "top") ? (def.recipe.limit || 10) : PREVIEW_ROW_CAP;
            if (items.length > 0) rows.push(rowFromDef(def, items.slice(0, cap)));
        }
        push({ pageKey: "anime", generation: generation, rows: rows, loading: loading === true, error: "" });
    }

    // Phase 1 — bundled MAL paints instantly (synchronous).
    if (mal && mal.ready && mal.ready()) {
        for (var i = 0; i < defs.length; i++) {
            var rows = malRowsFor(mal, defs[i].recipe, 0, PREVIEW_ROW_CAP);
            if (rows && rows.length) rowData[defs[i].key] = rows.map(mapJikan);
        }
    }
    publish(true);

    // Phase 2 — live keyless refresh (Jikan → Kitsu) for the hot shelves; failures keep bundled.
    var liveCancel = refreshAnimeLive(defs, rowData, function() { publish(true); }, function() { publish(false); });
    return function() {
        if (cancelled) return;
        cancelled = true;
        if (typeof liveCancel === "function") liveCancel();
    };
}

function loadCatalogPage(pageKey, options, push) {
    // Legacy shim: loadCatalogPage(pageKey, done) → forward every progressive push to done.
    if (typeof options === "function" && push === undefined) {
        var done = options;
        return loadCatalogPage(pageKey, {}, function(payload) { done(payload); });
    }
    options = options || {};
    push = push || function() {};
    if (pageKey === "anime") return loadAnimePageDeep(options, push);
    return loadMoviesShowsDeep(pageKey, options, push);
}

function findDef(pageKey, rowKey) {
    var defs = Rules.defaultRows(pageKey);
    if (pageKey === "movies") defs = defs.concat(Rules.dailyRows(Date.now(), 6));
    for (var i = 0; i < defs.length; i++) if (defs[i].key === rowKey) return defs[i];
    return null;
}

// Anime See-all paging: the bundled MAL catalogue supports true offset/limit paging, so the
// infinite grid pages the offline artifact (stable, keyless). A missing bundle or unknown key
// returns an honest state — never a silent reroute.
function loadAnimeRowPage(pin, offset, limit, options, done) {
    options = options || {};
    var generation = options.generation || 0;
    var mal = options.malCatalog || null;
    var explicitFilter = options.explicitFilter || null;
    var showExplicit = options.showExplicit === true;
    var def = findDef("anime", pin.rowKey);
    if (!def) { done({ generation: generation, items: [], hasMore: false, error: "unknown row: " + pin.rowKey }); return; }
    if (def.recipe.kind === "trending") {
        done({ generation: generation, items: [], hasMore: false, error: "" }); return;
    }
    if (LIVE_ANIME[pin.rowKey]) {
        var spec = LIVE_ANIME[pin.rowKey];
        var page = Math.floor(offset / limit) + 1;
        var params = {};
        for (var p in spec.params) params[p] = spec.params[p];
        params.page = page;
        return jikanFetch(spec.path, params, limit, function(items) {
            if (items && items.length) {
                if (explicitFilter) items = items.filter(function(it) { return explicitFilter(it, showExplicit); });
                done({ generation: generation, items: items, hasMore: items.length >= limit, error: "" });
            } else if (mal && mal.ready && mal.ready()) {
                var rows = malRowsFor(mal, def.recipe, offset, limit);
                var fbItems = rows.map(mapJikan);
                if (explicitFilter) fbItems = fbItems.filter(function(it) { return explicitFilter(it, showExplicit); });
                done({ generation: generation, items: fbItems, hasMore: rows.length >= limit, error: "" });
            } else {
                done({ generation: generation, items: [], hasMore: false, error: "anime catalogue offline" });
            }
        });
    }
    if (!mal || !mal.ready || !mal.ready()) {
        done({ generation: generation, items: [], hasMore: false, error: "anime catalogue offline" }); return;
    }
    var rows = malRowsFor(mal, def.recipe, offset, limit);
    var items = rows.map(mapJikan);
    if (explicitFilter) items = items.filter(function(it) { return explicitFilter(it, showExplicit); });
    done({ generation: generation, items: items, hasMore: rows.length >= limit, error: "" });
}
// Extension See-all paging. If the pinned catalogue is no longer installed/enabled/browsable,
// return an honest missing state that NAMES the provider (spec §8) — never a silent different
// catalogue. Otherwise page the real extension catalogue by offset.
function loadExtensionRowPage(pin, offset, limit, options, done) {
    options = options || {};
    var generation = options.generation || 0;
    var specs = AddonClient.theatreCatalogSpecs(extensionsList, pin.type);
    var found = null;
    for (var i = 0; i < specs.length; i++)
        if (specs[i].transportUrl === pin.transportUrl && specs[i].catalogId === pin.catalogId) { found = specs[i]; break; }
    if (!found) {
        done({ generation: generation, items: [], hasMore: false, missing: true,
               extName: pin.extName || "", error: "This catalogue is no longer available." });
        return;
    }
    var url = AddonClient.catalogUrl(found.transportUrl, found.type, found.catalogId, [], offset);
    return AddonClient.fetchCatalogUrl(url, function(metas) {
        var items = (metas || []).map(mapCinemeta);
        if (options.explicitFilter)
            items = items.filter(function(it) { return options.explicitFilter(it, options.showExplicit === true); });
        done({ generation: generation, items: items, hasMore: !!(metas && metas.length >= limit), error: "" });
    });
}

// Load the installed-extension shelves for a tab: recognized branded services (mainRows,
// placed into contextual slots among the house rows) and everything else (extensionRows, the
// "From Your Extensions" section). Each shelf fetches its real catalogue; empty ones drop out.
function loadExtensionRows(pageKey, contentType, houseDefs, options, onEach, onDone) {
    var specs = AddonClient.theatreCatalogSpecs(extensionsList, contentType);
    if (!specs.length) { onDone([], []); return function() {}; }
    var placement = Rules.placeExtensions(pageKey, specs, houseDefs);
    var rows = placement.mainRows.concat(placement.extensionRows);
    if (!rows.length) { onDone([], []); return function() {}; }
    var results = {}, pending = rows.length;
    var cancelled = false;
    var cancels = [];
    function settle() {
        if (cancelled) return;
        var main = [], ext = [];
        for (var k = 0; k < rows.length; k++) {
            var r = results[rows[k].key];
            if (!r) continue;
            var full = rowFromExtDef(r.row, r.items);
            if (r.row.sourceKind === "service-extension") main.push(full); else ext.push(full);
        }
        onDone(main, ext);
    }
    for (var i = 0; i < rows.length; i++) {
        (function(row) {
            var pin = row.seeAllPin;
            var url = AddonClient.catalogUrl(pin.transportUrl, pin.type, pin.catalogId, [], 0);
            var handle = AddonClient.fetchCatalogUrl(url, function(metas) {
                if (cancelled) return;
                var items = (metas || []).slice(0, EXTENSION_ROW_ITEM_CAP).map(mapCinemeta);
                if (options.explicitFilter)
                    items = items.filter(function(it) { return options.explicitFilter(it, options.showExplicit === true); });
                if (items.length) results[row.key] = { row: row, items: items };
                onEach();
                pending -= 1;
                if (pending === 0) settle();
            });
            cancels.push(handle);
        })(rows[i]);
    }
    return function() {
        if (cancelled) return;
        cancelled = true;
        for (var i = 0; i < cancels.length; i++)
            if (typeof cancels[i] === "function") cancels[i]();
    };
}

function rowFromExtDef(def, items) {
    return {
        key: def.key,
        title: def.title,
        pageKey: def.pageKey,
        ranked: false,
        rotating: false,
        sourceKind: def.sourceKind,
        sourceLabel: def.sourceLabel || def.extName || "",
        serviceKey: def.serviceKey || "",
        placement: def.placement,
        items: items,
        seeAllPin: def.seeAllPin
    };
}

// loadRowPage(pin, offset, limit, options, done): one See-all page. Expands the recipe's own
// catalogue window by offset and applies the SAME recipe + explicit filter. A missing row key
// returns an honest error — it is NEVER silently rerouted to Top 10.
function loadRowPage(pin, offset, limit, options, done) {
    options = options || {};
    var generation = options.generation || 0;
    limit = limit || 40;
    offset = offset || 0;
    if (!pin) { done({ generation: generation, items: [], hasMore: false, error: "missing pin" }); return; }
    if (pin.sourceKind === "extension" || pin.sourceKind === "service-extension") {
        return loadExtensionRowPage(pin, offset, limit, options, done);
    }
    if (pin.pageKey === "anime") { loadAnimeRowPage(pin, offset, limit, options, done); return; }
    var type = pin.pageKey === "shows" ? "series" : "movie";
    var def = findDef(pin.pageKey, pin.rowKey);
    if (!def) { done({ generation: generation, items: [], hasMore: false, error: "unknown row: " + pin.rowKey }); return; }
    var showExplicit = options.showExplicit === true;
    var explicitFilter = options.explicitFilter || null;
    var now = options.nowMs || Date.now();
    var queries = Rules.indexQueriesFor(def.recipe);
    if (queries.length > 0) {                          // index shelf: page the artifact
        var imdb = options.imdbCatalog || null;
        if (!imdb || !imdb.ready || !imdb.ready()) {
            done({ generation: generation, items: [], hasMore: false, error: "catalogue index offline" });
            return;
        }
        var rows = imdbRowsFor(imdb, def.recipe, offset, limit);
        var items = rows.map(mapImdb);
        if (explicitFilter) items = items.filter(function(it) { return explicitFilter(it, showExplicit); });
        done({ generation: generation, items: items, hasMore: rows.length >= limit, error: "" });
        return;
    }
    // live shelf (top-10 / recently-released / recently-premiered / currently-airing)
    return cinemetaCatalogPaged(type, "", offset, function(metas) {
        var mapped = (metas || []).map(mapCinemeta);
        var clean = filterLiveItems(options.imdbCatalog || null, mapped,
                                    def.recipe.kind === "recent");
        if (def.recipe.kind === "recent") clean = Rules.rankItems({ kind: "recent" }, clean, now);
        if (def.recipe.kind === "statusLive")
            clean = clean.filter(function(it) { return String(it.status || "") === "Continuing"; });
        if (explicitFilter) clean = clean.filter(function(it) { return explicitFilter(it, showExplicit); });
        done({ generation: generation, items: clean.slice(0, limit),
               hasMore: !!(metas && metas.length >= limit), error: "" });
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
