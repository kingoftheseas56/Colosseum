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

function MOVIE_ROWS() {
    return [
        house("top-10",             "Top 10",             0,   { kind: "top", limit: 10 }, true),
        house("recently-released",  "Recently Released",  10,  { kind: "recent" }),
        house("top-rated",          "Top Rated",          20,  { kind: "topRated", voteFloor: 25000 }),
        house("hidden-gems",        "Hidden Gems",        30,  { kind: "hiddenGems", voteFloor: 2000, popMax: 60000 }),
        house("all-time-greats",    "All-Time Greats",    40,  { kind: "topRated", voteFloor: 150000 }),
        house("under-two-hours",    "Under Two Hours",    50,  { kind: "runtimeUnder", maxMinutes: 120 }),
        house("documentary-movies", "Documentary Movies", 60,  { kind: "genre", genre: "Documentary" }),
        house("animated-movies",    "Animated Movies",    70,  { kind: "genre", genre: "Animation" }),
        house("international-cinema","International Cinema",80, { kind: "countryExclude", exclude: ["United States", "USA", "US", "United Kingdom", "UK"] }),
        house("japanese-cinema",    "Japanese Cinema",    90,  { kind: "country", countries: ["Japan"] }),
        house("korean-cinema",      "Korean Cinema",      100, { kind: "country", countries: ["South Korea", "Korea"] }),
        house("french-cinema",      "French Cinema",      110, { kind: "country", countries: ["France"] }),
        house("2020s-movies",       "2020s Movies",       120, { kind: "decade", from: 2020, to: 2029 }),
        house("2010s-movies",       "2010s Movies",       130, { kind: "decade", from: 2010, to: 2019 }),
        house("2000s-movies",       "2000s Movies",       140, { kind: "decade", from: 2000, to: 2009 }),
        house("1990s-movies",       "1990s Movies",       150, { kind: "decade", from: 1990, to: 1999 }),
        house("1980s-movies",       "1980s Movies",       160, { kind: "decade", from: 1980, to: 1989 }),
        house("1970s-movies",       "1970s Movies",       170, { kind: "decade", from: 1970, to: 1979 })
    ];
}

function SHOW_ROWS() {
    return [
        house("top-10",                     "Top 10",                     0,   { kind: "top", limit: 10 }, true),
        house("currently-airing",           "Currently Airing",           10,  { kind: "status", status: "Continuing" }),
        house("top-rated",                  "Top Rated",                  20,  { kind: "topRated", voteFloor: 15000 }),
        house("long-running-series",        "Long-Running Series",        30,  { kind: "longRunning", minSeasons: 4 }),
        house("recently-premiered",         "Recently Premiered",         40,  { kind: "recent" }),
        house("limited-series",             "Limited Series",             50,  { kind: "seasonExactly", seasons: 1 }),
        house("all-time-great-series",      "All-Time Great Series",      60,  { kind: "topRated", voteFloor: 80000 }),
        house("drama-series",               "Drama Series",               70,  { kind: "genre", genre: "Drama" }),
        house("comedy-series",              "Comedy Series",              80,  { kind: "genre", genre: "Comedy" }),
        house("crime-and-mystery",          "Crime and Mystery",          90,  { kind: "genreAny", genres: ["Crime", "Mystery"] }),
        house("science-fiction-and-fantasy","Science Fiction and Fantasy",100, { kind: "genreAny", genres: ["Sci-Fi", "Fantasy"] }),
        house("documentary-series",         "Documentary Series",         110, { kind: "genre", genre: "Documentary" }),
        house("animated-series",            "Animated Series",            120, { kind: "genre", genre: "Animation" }),
        house("korean-drama",               "Korean Drama",               130, { kind: "country", countries: ["South Korea", "Korea"] }),
        house("british-television",         "British Television",         140, { kind: "country", countries: ["United Kingdom", "UK"] })
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
    { key: "daily-crime-thrillers", title: "Crime Thrillers", recipe: { kind: "genre", genre: "Crime" } },
    { key: "daily-science-fiction", title: "Science Fiction", recipe: { kind: "genre", genre: "Sci-Fi" } },
    { key: "daily-family-movies",   title: "Family Movies",   recipe: { kind: "genre", genre: "Family" } },
    { key: "daily-90-minute",       title: "90-Minute Movies",recipe: { kind: "runtimeUnder", maxMinutes: 95 } },
    { key: "daily-classic-horror",  title: "Classic Horror",  recipe: { kind: "genre", genre: "Horror" } },
    { key: "daily-holiday-movies",  title: "Holiday Movies",  recipe: { kind: "genre", genre: "Holiday" } },
    { key: "daily-action",          title: "Action & Adventure", recipe: { kind: "genre", genre: "Action" } },
    { key: "daily-comedy",          title: "Comedy Night",    recipe: { kind: "genre", genre: "Comedy" } },
    { key: "daily-romance",         title: "Romance",         recipe: { kind: "genre", genre: "Romance" } },
    { key: "daily-mystery",         title: "Mystery",         recipe: { kind: "genre", genre: "Mystery" } },
    { key: "daily-fantasy",         title: "Fantasy",         recipe: { kind: "genre", genre: "Fantasy" } },
    { key: "daily-war",             title: "War Stories",     recipe: { kind: "genre", genre: "War" } },
    { key: "daily-westerns",        title: "Westerns",        recipe: { kind: "genre", genre: "Western" } },
    { key: "daily-animation",       title: "Animation",       recipe: { kind: "genre", genre: "Animation" } }
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
    var mean = recipe.mean || 6.5;

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
    if (kind === "topRated") {
        var floor = recipe.voteFloor || 1000;
        var scored = [];
        for (var i = 0; i < pool.length; i++) {
            var w = weighted(ratingOf(pool[i]) || 0, votesOf(pool[i]), mean, floor);
            if (w >= 0) scored.push({ it: pool[i], w: w });
        }
        scored.sort(function(a, b) { return b.w - a.w; });
        return scored.map(function(x) { return x.it; });
    }
    if (kind === "hiddenGems") {
        var floorH = recipe.voteFloor || 2000;
        var popMax = recipe.popMax !== undefined ? recipe.popMax : 250000;
        var popMin = recipe.popMin || 0;
        var gems = [];
        for (var g = 0; g < pool.length; g++) {
            var v = votesOf(pool[g]);
            var wg = weighted(ratingOf(pool[g]) || 0, v, mean, floorH);
            if (wg >= 0 && v <= popMax && v >= popMin) gems.push({ it: pool[g], w: wg });
        }
        gems.sort(function(a, b) { return b.w - a.w; });
        return gems.map(function(x) { return x.it; });
    }
    if (kind === "runtimeUnder") {
        var maxM = recipe.maxMinutes || 120;
        return pool.filter(function(it) { var r = runtimeMin(it); return r !== null && r <= maxM; });
    }
    if (kind === "genre") {
        return pool.filter(function(it) { return hasGenre(it, recipe.genre); });
    }
    if (kind === "genreAny") {
        var gs = recipe.genres || [];
        return pool.filter(function(it) {
            for (var q = 0; q < gs.length; q++) if (hasGenre(it, gs[q])) return true;
            return false;
        });
    }
    if (kind === "country") {
        return pool.filter(function(it) { return countryMatches(it, recipe.countries || [recipe.country]); });
    }
    if (kind === "countryExclude") {
        var ex = recipe.exclude || [];
        return pool.filter(function(it) {
            if (!String(it.country || "")) return false;   // must know the country to place it
            return !countryMatches(it, ex);
        });
    }
    if (kind === "decade") {
        return pool.filter(function(it) { var y = yearOf(it); return y !== null && y >= recipe.from && y <= recipe.to; });
    }
    if (kind === "status") {
        var want = String(recipe.status).toLowerCase();
        return pool.filter(function(it) { return String(it.status || "").toLowerCase() === want; });
    }
    if (kind === "longRunning") {
        var minS = recipe.minSeasons || 2;
        var lr = pool.filter(function(it) { return (it.seasonCount || 0) >= minS; });
        lr.sort(function(a, b) { return (b.seasonCount || 0) - (a.seasonCount || 0); });
        return lr;
    }
    if (kind === "seasonExactly") {
        var want2 = recipe.seasons || 1;
        return pool.filter(function(it) { return it.seasonCount === want2; });
    }
    return pool;
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
