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
// test-only: drop the live (Jikan/Kitsu) + full-meta caches so harness scenarios don't bleed.
function resetLiveCaches() {
    jikanCache = {}; jikanInflight = {}; metaCache = {}; metaInFlight = {};
}

// Deep-catalogue enrichment tuning (spec §12). Full-meta enrichment is bounded to four
// concurrent requests, deduplicated + coalesced by URL, and cached for 30 minutes.
var MAX_META_WORKERS = 4;
var META_CACHE_TTL_MS = 30 * 60 * 1000;
var PREVIEW_ROW_CAP = 20;
var ENRICH_CAP = 48;
var metaCache = {};       // url -> { t, value }
var metaInFlight = {};    // url -> [done, ...]

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
    if (requestAdapter) { requestAdapter(url, done); return; }
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
        // Posters: force `small` — the ONLY size metahub reliably has. Upscaling small→medium
        // 404'd the long tail (metahub lacks a medium for many titles, e.g. tt2431250) → permanent
        // blank tiles; `small` also matches the Top-list tile size. (Hemanth eyes-on 2026-07-25.)
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
    requestJsonWithFallback(urls, function(json) {
        done(json && json.metas ? json.metas : []);
    });
}

function fullMetaUrl(type, id) {
    var sType = (type === "series") ? "series" : "movie";
    return CINEMETA + "/meta/" + sType + "/" + id + ".json";
}

// URL-keyed, 30-min cached, in-flight-coalesced full meta: concurrent callers for one URL
// collapse to a SINGLE request (spec §12 — deduplicated + coalesced).
function loadMetaCached(type, id, done) {
    var url = fullMetaUrl(type, id);
    var now = Date.now();
    var hit = metaCache[url];
    if (hit && (now - hit.t) < META_CACHE_TTL_MS) { done(hit.value); return; }
    if (metaInFlight[url]) { metaInFlight[url].push(done); return; }
    metaInFlight[url] = [done];
    requestJson(url, function(json) {
        var meta = json && json.meta ? json.meta : null;
        metaCache[url] = { t: Date.now(), value: meta };
        var waiters = metaInFlight[url] || [];
        delete metaInFlight[url];
        for (var i = 0; i < waiters.length; i++) waiters[i](meta);
    });
}

function collectGenres(defs) {
    var set = {}, out = [];
    for (var i = 0; i < defs.length; i++) {
        var r = defs[i].recipe || {};
        if (r.kind === "genre" && r.genre && !set[r.genre]) { set[r.genre] = true; out.push(r.genre); }
        else if (r.kind === "genreAny" && r.genres)
            for (var j = 0; j < r.genres.length; j++)
                if (!set[r.genres[j]]) { set[r.genres[j]] = true; out.push(r.genres[j]); }
    }
    return out;
}

function collectNeededFields(defs) {
    var need = {};
    for (var i = 0; i < defs.length; i++) {
        var k = (defs[i].recipe || {}).kind;
        if (k === "runtimeUnder") need.runtime = true;
        else if (k === "country" || k === "countryExclude") need.country = true;
        else if (k === "status") need.status = true;
        else if (k === "longRunning" || k === "seasonExactly") need.season = true;
    }
    return need;
}

function itemMissing(item, need) {
    if (need.runtime && !item.runtime) return true;
    if (need.country && !item.country) return true;
    if (need.status && !item.status) return true;
    if (need.season && !item.videosKnown) return true;
    return false;
}

function mergeMetaFields(item, meta) {
    if (!meta) return;
    if (!item.runtime && meta.runtime) item.runtime = meta.runtime;
    if (!item.country && meta.country) item.country = meta.country;
    if (!item.status && meta.status) item.status = meta.status;
    if (!item.imdbRating && meta.imdbRating) item.imdbRating = meta.imdbRating;
    if ((!item.genres || !item.genres.length) && (meta.genres || meta.genre))
        item.genres = meta.genres || meta.genre;
    if (meta.videos !== undefined) { item.videosKnown = true; item.seasonCount = seasonCount(meta.videos); }
}

// One deduped candidate pool: top + each required genre catalog. Genre-catalog items are
// tagged with that genre; duplicates merge genre lists. Sequential fetches bound transport
// and let the page publish progressively; a failed genre fetch simply adds nothing.
function buildPool(type, genres, onProgress, onDone) {
    var sources = [""].concat(genres);
    var pool = [], byId = {}, idx = 0;
    function addMetas(genre, metas) {
        for (var k = 0; k < metas.length; k++) {
            var item = mapCinemeta(metas[k], pool.length + k);
            if (!item.id) continue;
            if (genre && item.genres.indexOf(genre) === -1) item.genres = item.genres.concat([genre]);
            var ex = byId[item.id];
            if (ex) {
                for (var g = 0; g < item.genres.length; g++)
                    if (ex.genres.indexOf(item.genres[g]) === -1) ex.genres.push(item.genres[g]);
            } else { byId[item.id] = item; pool.push(item); }
        }
    }
    function next() {
        if (idx >= sources.length) { onDone(pool); return; }
        var genre = sources[idx]; idx++;
        cinemetaCatalogPaged(type, genre, 0, function(metas) {
            addMetas(genre, metas || []);
            onProgress(pool.slice());
            next();
        });
    }
    next();
}

// Bounded four-worker full-meta enrichment for fact shelves. Only capped pool items missing
// a needed field are enriched; requests coalesce + cache by URL; never exceeds MAX_META_WORKERS.
function enrichPool(type, pool, need, onProgress, onDone) {
    if (!(need.runtime || need.country || need.status || need.season)) { onDone(); return; }
    var targets = [];
    for (var i = 0; i < pool.length && targets.length < ENRICH_CAP; i++)
        if (itemMissing(pool[i], need)) targets.push(pool[i]);
    if (!targets.length) { onDone(); return; }
    var nextIdx = 0, active = 0, finished = 0;
    function pump() {
        while (active < MAX_META_WORKERS && nextIdx < targets.length) {
            var item = targets[nextIdx]; nextIdx++; active++;
            (function(it) {
                loadMetaCached(type, it.id, function(meta) {
                    mergeMetaFields(it, meta);
                    active--; finished++;
                    onProgress();
                    if (finished >= targets.length) onDone(); else pump();
                });
            })(item);
        }
    }
    pump();
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
    var now = options.nowMs || Date.now();
    var explicitFilter = options.explicitFilter || null;

    var defs = Rules.defaultRows(pageKey);
    if (pageKey === "movies") defs = defs.concat(Rules.dailyRows(now, 6));
    defs.sort(function(a, b) { return a.placement - b.placement; });

    var pool = [];
    function keep(item) { return explicitFilter ? explicitFilter(item, showExplicit) : true; }
    function publish(loading) {
        var visible = pool.filter(keep);
        var rows = [];
        for (var i = 0; i < defs.length; i++) {
            var def = defs[i];
            var cap = (def.recipe.kind === "top") ? (def.recipe.limit || 10) : PREVIEW_ROW_CAP;
            var ranked = Rules.rankItems(def.recipe, visible, now).slice(0, cap);
            if (ranked.length > 0) rows.push(rowFromDef(def, ranked));
        }
        push({ pageKey: pageKey, generation: generation, rows: rows, loading: loading === true, error: "" });
    }

    var genres = collectGenres(defs);
    var need = collectNeededFields(defs);
    buildPool(type, genres, function(partial) {
        pool = partial; publish(true);
    }, function(full) {
        pool = full; publish(true);
        enrichPool(type, pool, need, function() { publish(true); }, function() { publish(false); });
    });
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
    if (!keys.length) { onDone(); return; }
    var pending = keys.length;
    for (var k = 0; k < keys.length; k++) {
        (function(key) {
            var spec = LIVE_ANIME[key];
            jikanFetch(spec.path, spec.params, PREVIEW_ROW_CAP, function(items) {
                // jikanFetch already falls back to Kitsu; only overwrite when a live source
                // actually produced items, so a total live failure leaves the bundled row intact.
                if (items && items.length) rowData[key] = items;
                onRefresh();
                pending -= 1;
                if (pending === 0) onDone();
            });
        })(keys[k]);
    }
}

function loadAnimePageDeep(options, push) {
    var generation = options.generation || 0;
    var showExplicit = options.showExplicit === true;
    var explicitFilter = options.explicitFilter || null;
    var mal = options.malCatalog || null;

    var defs = Rules.defaultRows("anime");
    defs.sort(function(a, b) { return a.placement - b.placement; });

    var rowData = {};   // key -> mapped card items (best source so far)
    function keep(items) {
        if (!explicitFilter) return items;
        return items.filter(function(it) { return explicitFilter(it, showExplicit); });
    }
    function publish(loading) {
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
    refreshAnimeLive(defs, rowData, function() { publish(true); }, function() { publish(false); });
}

function loadCatalogPage(pageKey, options, push) {
    // Legacy shim: loadCatalogPage(pageKey, done) → forward every progressive push to done.
    if (typeof options === "function" && push === undefined) {
        var done = options;
        loadCatalogPage(pageKey, {}, function(payload) { done(payload); });
        return;
    }
    options = options || {};
    push = push || function() {};
    if (pageKey === "anime") { loadAnimePageDeep(options, push); return; }
    loadMoviesShowsDeep(pageKey, options, push);
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
    if (!mal || !mal.ready || !mal.ready()) {
        done({ generation: generation, items: [], hasMore: false, error: "anime catalogue offline" }); return;
    }
    if (def.recipe.kind === "trending") {
        done({ generation: generation, items: [], hasMore: false, error: "" }); return;
    }
    var rows = malRowsFor(mal, def.recipe, offset, limit);
    var items = rows.map(mapJikan);
    if (explicitFilter) items = items.filter(function(it) { return explicitFilter(it, showExplicit); });
    done({ generation: generation, items: items, hasMore: rows.length >= limit, error: "" });
}
function loadExtensionRowPage(pin, offset, limit, options, done) {
    done({ generation: (options || {}).generation || 0, items: [], hasMore: false, error: "" });
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
        loadExtensionRowPage(pin, offset, limit, options, done); return;
    }
    if (pin.pageKey === "anime") { loadAnimeRowPage(pin, offset, limit, options, done); return; }
    var type = pin.pageKey === "shows" ? "series" : "movie";
    var def = findDef(pin.pageKey, pin.rowKey);
    if (!def) { done({ generation: generation, items: [], hasMore: false, error: "unknown row: " + pin.rowKey }); return; }
    var showExplicit = options.showExplicit === true;
    var explicitFilter = options.explicitFilter || null;
    var now = options.nowMs || Date.now();
    var recipe = def.recipe;
    var genre = (recipe.kind === "genre") ? recipe.genre : "";
    cinemetaCatalogPaged(type, genre, offset, function(metas) {
        var mapped = (metas || []).map(mapCinemeta);
        var visible = explicitFilter ? mapped.filter(function(it) { return explicitFilter(it, showExplicit); }) : mapped;
        var ranked = Rules.rankItems(recipe, visible, now).slice(0, limit);
        done({ generation: generation, items: ranked, hasMore: !!(metas && metas.length >= limit), error: "" });
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
