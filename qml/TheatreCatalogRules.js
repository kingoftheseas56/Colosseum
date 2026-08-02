// TheatreCatalogRules.js — the pure, keyless brain of the deep Theatre catalogue.
// Stable shelf inventories, ranking predicates, deterministic daily rotation, extension
// placement, and row customization. NO transport, NO Date.now(), NO QML context: every
// function is a pure transform so the offscreen rules harness can pin it. Titles are literal
// and self-explanatory — there is intentionally no `sub`/`blurb` field on any row (spec §4).
.pragma library

// ---------------------------------------------------------------------------
// House shelf inventories (spec §5). `recipe` drives rankItems; `placement` is the
// stable ordering slot the page sorts by; `key` (never the title) owns persistence.
// ---------------------------------------------------------------------------

function house(key, title, placement, recipe, ranked) {
    return {
        key: key,
        title: title,
        pageKey: recipe && recipe.pageKey ? recipe.pageKey : "",
        placement: placement,
        ranked: ranked === true,
        sourceKind: "house",
        sourceLabel: "Colosseum",
        rotating: false,
        recipe: recipe
    };
}

// ── Threshold dials (spec §4.3). Task 6 calibrates against REAL output; tests assert
// relationships only. IMDb votes run ~100× TMDB's (Shawshank: 3.2M).
var THRESHOLDS = {
    movie:  { TR_RATING: 8.0, TR_VOTES: 200000,
              HG_RATING: 7.4, HG_VOTES_MIN: 10000, HG_VOTES_MAX: 100000,
              CC_RATING: 7.2, CC_VOTES_MIN: 10000, CC_VOTES_MAX: 250000 },
    series: { TR_RATING: 8.2, TR_VOTES: 100000,
              HG_RATING: 7.5, HG_VOTES_MIN: 5000, HG_VOTES_MAX: 75000,
              CC_RATING: 7.5, CC_VOTES_MIN: 5000, CC_VOTES_MAX: 150000 },
    RECENT_VOTE_FLOOR: 500,
    GENRE_VOTE_FLOOR: 5000,
    LR_EPISODES: 100
};

function MOVIE_ROWS() {
    var T = THRESHOLDS.movie;
    return [
        house("top-10",             "Top 10",              0,   { kind: "top", limit: 10 }, true),
        house("recently-released",  "Recently Released",   10,  { kind: "recent" }),
        house("top-rated",          "Top Rated",           20,  { kind: "imdbBand", type: "movie", order: "rating", ratingMin: T.TR_RATING, votesMin: T.TR_VOTES }),
        house("hidden-gems",        "Hidden Gems",         30,  { kind: "imdbBand", type: "movie", order: "rating", ratingMin: T.HG_RATING, votesMin: T.HG_VOTES_MIN, votesMax: T.HG_VOTES_MAX }),
        house("cult-classics",      "Cult Classics",       40,  { kind: "imdbBand", type: "movie", order: "rating", ratingMin: T.CC_RATING, votesMin: T.CC_VOTES_MIN, votesMax: T.CC_VOTES_MAX, yearTo: 1999 }),
        house("under-two-hours",    "Under Two Hours",     50,  { kind: "imdbBand", type: "movie", order: "votes", runtimeMax: 120, votesMin: THRESHOLDS.GENRE_VOTE_FLOOR }),
        house("documentary-movies", "Documentary Movies",  60,  { kind: "imdbGenre", type: "movie", genre: "Documentary" }),
        house("animated-movies",    "Animated Movies",     70,  { kind: "imdbGenre", type: "movie", genre: "Animation" }),
        house("international-cinema","International Cinema",80, { kind: "imdbIntl", type: "movie" }),
        house("japanese-cinema",    "Japanese Cinema",     90,  { kind: "imdbLang", type: "movie", lang: "ja" }),
        house("korean-cinema",      "Korean Cinema",       100, { kind: "imdbLang", type: "movie", lang: "ko" }),
        house("french-cinema",      "French Cinema",       110, { kind: "imdbLang", type: "movie", lang: "fr" }),
        house("2020s-movies",       "2020s Movies",        120, { kind: "imdbDecade", type: "movie", from: 2020, to: 2029 }),
        house("2010s-movies",       "2010s Movies",        130, { kind: "imdbDecade", type: "movie", from: 2010, to: 2019 }),
        house("2000s-movies",       "2000s Movies",        140, { kind: "imdbDecade", type: "movie", from: 2000, to: 2009 }),
        house("1990s-movies",       "1990s Movies",        150, { kind: "imdbDecade", type: "movie", from: 1990, to: 1999 }),
        house("1980s-movies",       "1980s Movies",        160, { kind: "imdbDecade", type: "movie", from: 1980, to: 1989 }),
        house("1970s-movies",       "1970s Movies",        170, { kind: "imdbDecade", type: "movie", from: 1970, to: 1979 })
    ];
}

function SHOW_ROWS() {
    var T = THRESHOLDS.series;
    return [
        house("top-10",              "Top 10",                      0,   { kind: "top", limit: 10 }, true),
        house("currently-airing",    "Currently Airing",            10,  { kind: "statusLive", status: "Continuing" }),
        house("recently-premiered",  "Recently Premiered",          20,  { kind: "recent" }),
        house("top-rated",           "Top Rated",                   30,  { kind: "imdbBand", type: "series", order: "rating", ratingMin: T.TR_RATING, votesMin: T.TR_VOTES }),
        house("hidden-gems",         "Hidden Gems",                 40,  { kind: "imdbBand", type: "series", order: "rating", ratingMin: T.HG_RATING, votesMin: T.HG_VOTES_MIN, votesMax: T.HG_VOTES_MAX }),
        house("cult-classics",       "Cult Classics",               50,  { kind: "imdbBand", type: "series", order: "rating", ratingMin: T.CC_RATING, votesMin: T.CC_VOTES_MIN, votesMax: T.CC_VOTES_MAX, yearTo: 1999 }),
        house("long-running-series", "Long-Running Series",         60,  { kind: "imdbBand", type: "series", order: "episodes", episodesMin: THRESHOLDS.LR_EPISODES }),
        house("limited-series",      "Limited Series",              70,  { kind: "imdbBand", type: "mini", order: "votes", votesMin: THRESHOLDS.GENRE_VOTE_FLOOR }),
        house("drama-series",        "Drama Series",                80,  { kind: "imdbGenre", type: "series", genre: "Drama" }),
        house("comedy-series",       "Comedy Series",               90,  { kind: "imdbGenre", type: "series", genre: "Comedy" }),
        house("crime-and-mystery",   "Crime and Mystery",           100, { kind: "imdbGenreAny", type: "series", genres: ["Crime", "Mystery"] }),
        house("science-fiction-and-fantasy", "Science Fiction and Fantasy", 110, { kind: "imdbGenreAny", type: "series", genres: ["Sci-Fi", "Fantasy"] }),
        house("documentary-series",  "Documentary Series",          120, { kind: "imdbGenre", type: "series", genre: "Documentary" }),
        house("animated-series",     "Animated Series",             130, { kind: "imdbGenre", type: "series", genre: "Animation" }),
        house("korean-drama",        "Korean Drama",                140, { kind: "imdbLang", type: "series", lang: "ko" })
    ];
}

function ANIME_ROWS() {
    return [
        house("top-10",              "Top 10",              0,   { kind: "top", limit: 10, source: "top" }, true),
        house("trending",            "Trending",            10,  { kind: "trending" }),
        house("airing-now",          "Airing Now",          20,  { kind: "animeStatus", status: "Currently Airing", order: "members" }),
        house("top-airing",          "Top Airing",          30,  { kind: "animeStatus", status: "Currently Airing", order: "score" }),
        house("upcoming-season",     "Upcoming Season",     40,  { kind: "animeStatus", status: "Not yet aired", order: "members" }),
        house("top-series",          "Top Series",          50,  { kind: "animeType", type: "TV", order: "score", voteFloor: 5000 }),
        house("top-anime-movies",    "Top Anime Movies",    60,  { kind: "animeType", type: "Movie", order: "score", voteFloor: 5000 }),
        house("most-popular",        "Most Popular",        70,  { kind: "animeOrder", order: "members" }),
        house("top-rated",           "Top Rated",           80,  { kind: "animeOrder", order: "score", voteFloor: 5000 }),
        house("hidden-gems",         "Hidden Gems",         90,  { kind: "animeGems", voteFloor: 2000, membersMin: 20000, membersMax: 150000 }),
        house("2020s-anime",         "2020s Anime",         100, { kind: "animeDecade", from: 2020, to: 2029 }),
        house("2010s-anime",         "2010s Anime",         110, { kind: "animeDecade", from: 2010, to: 2019 }),
        house("2000s-anime",         "2000s Anime",         120, { kind: "animeDecade", from: 2000, to: 2009 }),
        house("1990s-earlier",       "1990s and Earlier",   130, { kind: "animeDecade", from: 0, to: 1999 }),
        house("action-and-adventure","Action and Adventure",140, { kind: "animeTag", tag: "Action" }),
        house("romance",             "Romance",             150, { kind: "animeTag", tag: "Romance" }),
        house("slice-of-life",       "Slice of Life",       160, { kind: "animeTag", tag: "Slice of Life" }),
        house("mecha",               "Mecha",               170, { kind: "animeTag", tag: "Mecha" }),
        house("fantasy",             "Fantasy",             180, { kind: "animeTag", tag: "Fantasy" }),
        house("science-fiction",     "Science Fiction",     190, { kind: "animeTag", tag: "Sci-Fi" }),
        house("psychological",       "Psychological",       200, { kind: "animeTag", tag: "Psychological" }),
        house("horror-and-supernatural","Horror and Supernatural",210, { kind: "animeTagAny", tags: ["Horror", "Supernatural"] })
    ];
}

function defaultRows(pageKey) {
    var rows = pageKey === "shows" ? SHOW_ROWS()
             : pageKey === "anime" ? ANIME_ROWS()
             : MOVIE_ROWS();
    for (var i = 0; i < rows.length; i++)
        rows[i].pageKey = pageKey === "shows" ? "shows" : pageKey === "anime" ? "anime" : "movies";
    return rows;
}

// ---------------------------------------------------------------------------
// Deterministic daily rotation for Movies (spec §5.1, decision 8). Same UTC day yields
// the same ordered subset; the seed is the integer UTC-day index so time-of-day cannot
// shift it. A stable per-recipe key means a hidden/renamed daily shelf keeps its
// preference whenever that key recurs.
// ---------------------------------------------------------------------------

var MOVIE_DAILY_POOL = [
    { key: "daily-crime-thrillers", title: "Crime Thrillers",  recipe: { kind: "imdbGenre", type: "movie", genre: "Crime" } },
    { key: "daily-science-fiction", title: "Science Fiction",  recipe: { kind: "imdbGenre", type: "movie", genre: "Sci-Fi" } },
    { key: "daily-family-movies",   title: "Family Movies",    recipe: { kind: "imdbGenre", type: "movie", genre: "Family" } },
    { key: "daily-90-minute",       title: "90-Minute Movies", recipe: { kind: "imdbBand", type: "movie", order: "votes", runtimeMax: 95, votesMin: THRESHOLDS.GENRE_VOTE_FLOOR } },
    { key: "daily-classic-horror",  title: "Classic Horror",   recipe: { kind: "imdbGenre", type: "movie", genre: "Horror", yearTo: 1999 } },
    { key: "daily-war",             title: "War Stories",      recipe: { kind: "imdbGenre", type: "movie", genre: "War" } },
    { key: "daily-westerns",        title: "Westerns",         recipe: { kind: "imdbGenre", type: "movie", genre: "Western" } },
    { key: "daily-mystery",         title: "Mystery",          recipe: { kind: "imdbGenre", type: "movie", genre: "Mystery" } },
    { key: "daily-romance",         title: "Romance",          recipe: { kind: "imdbGenre", type: "movie", genre: "Romance" } },
    { key: "daily-spanish",         title: "Spanish-Language Cinema", recipe: { kind: "imdbLang", type: "movie", lang: "es" } },
    { key: "daily-italian",         title: "Italian Cinema",   recipe: { kind: "imdbLang", type: "movie", lang: "it" } },
    { key: "daily-german",          title: "German Cinema",    recipe: { kind: "imdbLang", type: "movie", lang: "de" } },
    { key: "daily-swedish",         title: "Swedish Cinema",   recipe: { kind: "imdbLang", type: "movie", lang: "sv" } },
    { key: "daily-danish",          title: "Danish Cinema",    recipe: { kind: "imdbLang", type: "movie", lang: "da" } }
];

function dayIndex(dateMs) {
    return Math.floor(dateMs / 86400000);
}

// mulberry32 — deterministic PRNG seeded by an integer (no Math.random()).
function seededRng(seed) {
    var s = (seed >>> 0) || 1;
    return function() {
        s = (s + 0x6D2B79F5) | 0;
        var t = Math.imul(s ^ (s >>> 15), 1 | s);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

function dailyRows(dateMs, count) {
    var n = count || 6;
    var pool = MOVIE_DAILY_POOL.slice();
    var rng = seededRng(dayIndex(dateMs) + 1);
    for (var i = pool.length - 1; i > 0; i--) {
        var j = Math.floor(rng() * (i + 1));
        var tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
    }
    var out = [];
    for (var k = 0; k < Math.min(n, pool.length); k++) {
        var d = pool[k];
        out.push({
            key: d.key,
            title: d.title,
            pageKey: "movies",
            placement: 800 + k,
            ranked: false,
            sourceKind: "house",
            sourceLabel: "Colosseum",
            rotating: true,
            recipe: d.recipe
        });
    }
    return out;
}

// ---------------------------------------------------------------------------
// Item field readers — tolerant of missing facts. A missing fact returns null/0 so a
// fact-dependent recipe can EXCLUDE it rather than fabricate a value (spec §6.1).
// ---------------------------------------------------------------------------

function canonicalId(item) {
    return item.id || item.imdb_id || item.caption || item.title || "";
}
function yearOf(item) {
    var m = String(item.releaseInfo || item.year || "").match(/(\d{4})/);
    return m ? parseInt(m[1], 10) : null;   // null == undated -> excluded from dated shelves
}
function ratingOf(item) {
    var r = parseFloat(item.imdbRating !== undefined ? item.imdbRating : item.score);
    return isFinite(r) && r > 0 ? r : null;
}
function votesOf(item) {
    var v = parseInt(item.votes !== undefined ? item.votes
          : item.imdbVotes !== undefined ? item.imdbVotes
          : item.scored_by !== undefined ? item.scored_by
          : item.members !== undefined ? item.members : 0, 10);
    return isFinite(v) && v > 0 ? v : 0;
}
function runtimeMin(item) {
    var m = String(item.runtime || "").match(/(\d+)/);
    return m ? parseInt(m[1], 10) : null;   // null == unknown runtime -> excluded
}
function hasGenre(item, g) {
    var arr = item.genres || [];
    for (var i = 0; i < arr.length; i++)
        if (String(arr[i]).toLowerCase() === String(g).toLowerCase()) return true;
    return false;
}
function countryMatches(item, wanted) {
    var c = String(item.country || "").toLowerCase();
    if (!c) return false;                   // missing country -> excluded
    for (var i = 0; i < wanted.length; i++)
        if (c.indexOf(String(wanted[i]).toLowerCase()) !== -1) return true;
    return false;
}

function weighted(score, votes, mean, floor) {
    if (!(score > 0) || !(votes >= floor)) return -1;
    return (votes / (votes + floor)) * score + (floor / (votes + floor)) * mean;
}

function dedupe(items) {
    var seen = {}, out = [];
    for (var i = 0; i < items.length; i++) {
        var k = canonicalId(items[i]);
        if (!k || seen[k]) continue;
        seen[k] = true;
        out.push(items[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// rankItems — the single ranking/filtering transform. It NEVER fabricates a fact and
// NEVER resurrects an item the caller pre-filtered (e.g. explicit titles removed by
// ExplicitContentPolicy upstream). Anime recipes are pre-satisfied by MalCatalog/Jikan
// queries (Task 2/4), so their rankItems path is a dedupe + optional vote-floor pass.
// ---------------------------------------------------------------------------

function rankItems(recipe, items, nowMs) {
    recipe = recipe || {};
    var pool = dedupe(items || []);
    var kind = recipe.kind || "top";

    if (kind === "top" || kind === "extension" || kind === "trending"
        || kind === "animeStatus" || kind === "animeType" || kind === "animeOrder"
        || kind === "animeDecade" || kind === "animeTag" || kind === "animeTagAny"
        || kind === "animeGems") {
        // Source-ordered recipes: the query already applied the semantic filter (offset/limit,
        // status/type/year/tag). Keep source order, cap for ranked Top 10.
        return pool.slice(0, recipe.limit || pool.length);
    }
    if (kind === "recent") {
        var dated = pool.filter(function(it) { return yearOf(it) !== null; });
        dated.sort(function(a, b) {
            var d = yearOf(b) - yearOf(a);
            return d !== 0 ? d : pool.indexOf(a) - pool.indexOf(b);
        });
        return dated;
    }
    // The IMDb-backed recipes (imdbBand / imdbGenre / imdbGenreAny / imdbLang / imdbIntl /
    // imdbDecade) are satisfied by ImdbCatalog server-side (Task 4); rankItems never sees
    // them. statusLive rows are a live pass-through handled by TheatreApi. Anything else is
    // returned as a deduped passthrough.
    return pool;
}

// ── recipe -> ONE allowlisted ImdbCatalog query; null for live recipes.
function indexQueryFor(recipe) {
    recipe = recipe || {};
    function base(extra) {
        var q = { type: recipe.type, excludeAnime: true };
        for (var k in extra) if (extra[k] !== undefined) q[k] = extra[k];
        return q;
    }
    switch (recipe.kind) {
    case "imdbBand":
        return base({ order: recipe.order || "rating", ratingMin: recipe.ratingMin,
                      votesMin: recipe.votesMin, votesMax: recipe.votesMax,
                      yearTo: recipe.yearTo, runtimeMax: recipe.runtimeMax,
                      episodesMin: recipe.episodesMin });
    case "imdbGenre":
        return base({ order: "votes", genre: recipe.genre, yearTo: recipe.yearTo,
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR });
    case "imdbLang":
        // Language shelves are live-action. Exclude Animation (catches anime the Fribb set missed,
        // e.g. Bleach TYBW); series shelves also drop non-scripted formats so "Korean Drama" is drama,
        // not a variety/game-show list. The residual (a Chinese title mis-derived as ko) is accepted.
        return base({ order: "rating", lang: recipe.lang,
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR,
                      notGenre: recipe.type === "series"
                          ? ["Animation", "Reality-TV", "Game-Show", "Talk-Show", "News"]
                          : ["Animation"] });
    case "imdbIntl":
        return base({ order: "votes", notLang: "en",
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR,
                      notGenre: ["Animation"] });
    case "imdbDecade":
        return base({ order: "votes", yearFrom: recipe.from, yearTo: recipe.to,
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR });
    default:
        return null;    // top / recent / statusLive / imdbGenreAny (fan-out) / extension
    }
}

// ── genreAny recipes fan out to one query per genre; caller merges + dedupes.
function indexQueriesFor(recipe) {
    if (!recipe || recipe.kind !== "imdbGenreAny") {
        var one = indexQueryFor(recipe);
        return one ? [one] : [];
    }
    return (recipe.genres || []).map(function(g) {
        return indexQueryFor({ kind: "imdbGenre", type: recipe.type, genre: g });
    });
}

// ---------------------------------------------------------------------------
// Extension placement (spec §8). Recognized branded services slot into declared
// contextual positions in the main list; every other compatible catalogue lands under
// `From Your Extensions` in installed order. Callers pass only enabled, browsable
// (no required-extra) catalogues — disabled/required ones are filtered upstream.
// ---------------------------------------------------------------------------

var SERVICE_SLOTS = {
    netflix:  15,
    prime:    16,
    disney:   17,
    hbo:      18,
    max:      18,
    appletv:  19,
    amc:      25,
    fx:       26
};

function placeExtensions(pageKey, installed, houses) {
    installed = installed || [];
    var mainRows = [], extensionRows = [];
    for (var i = 0; i < installed.length; i++) {
        var ext = installed[i];
        var svc = ext.serviceKey ? String(ext.serviceKey).toLowerCase() : "";
        var recognized = svc && SERVICE_SLOTS[svc] !== undefined;
        var base = {
            key: "ext:" + (ext.transportUrl || ext.extName || i) + ":" + (ext.catalogId || ""),
            title: ext.title || ext.extName || "Catalogue",
            pageKey: pageKey,
            ranked: false,
            rotating: false,
            sourceKind: recognized ? "service-extension" : "extension",
            sourceLabel: ext.extName || "",
            extName: ext.extName || "",
            serviceKey: svc,
            recipe: { kind: "extension" },
            installedOrder: i,
            seeAllPin: {
                pageKey: pageKey,
                sourceKind: recognized ? "service-extension" : "extension",
                rowKey: "ext:" + (ext.transportUrl || ext.extName || i) + ":" + (ext.catalogId || ""),
                transportUrl: ext.transportUrl,
                type: ext.type,
                catalogId: ext.catalogId,
                extName: ext.extName
            }
        };
        if (recognized) {
            base.placement = SERVICE_SLOTS[svc];
            mainRows.push(base);
        } else {
            base.placement = 2000 + i;
            extensionRows.push(base);
        }
    }
    return { mainRows: mainRows, extensionRows: extensionRows };
}

// ---------------------------------------------------------------------------
// applyCustomization (spec §11). Orders saved keys first, appends new keys in default
// order, ignores removed keys, includes hidden rows only in edit mode, and applies
// renamed labels on COPIES so the source inventory is never mutated.
// ---------------------------------------------------------------------------

function applyCustomization(rows, custom, editMode) {
    custom = custom || {};
    var order = custom.order || [];
    var hidden = custom.hidden || [];
    var renamed = custom.renamed || {};
    var byKey = {}, hiddenSet = {}, placed = {}, out = [];
    for (var i = 0; i < rows.length; i++) byKey[rows[i].key] = rows[i];
    for (var h = 0; h < hidden.length; h++) hiddenSet[hidden[h]] = true;

    function emit(src) {
        var isHidden = !!hiddenSet[src.key];
        if (isHidden && !editMode) return;
        var copy = {};
        for (var k in src) copy[k] = src[k];
        var rn = renamed[src.key];
        if (rn !== undefined && rn !== null && String(rn).length > 0) copy.title = rn;
        copy.hidden = isHidden;
        out.push(copy);
    }
    for (var o = 0; o < order.length; o++) {
        var key = order[o];
        if (byKey[key] && !placed[key]) { placed[key] = true; emit(byKey[key]); }
    }
    for (var r = 0; r < rows.length; r++) {
        if (!placed[rows[r].key]) { placed[rows[r].key] = true; emit(rows[r]); }
    }
    return out;
}
