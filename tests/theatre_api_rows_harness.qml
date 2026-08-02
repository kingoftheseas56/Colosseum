// Offscreen proof of TheatreApi's deep keyless Movies/Shows + Anime engine. A transport
// adapter feeds deterministic Cinemeta fixtures; a fake ImdbCatalog serves index rows and
// facts. NEVER throw offscreen: collect fails, print the OK marker only when clean, single
// Qt.exit(fails.length).
import QtQuick
import "../qml/TheatreApi.js" as TheatreApi

Item {
    id: harness

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    // ---- fake ImdbCatalog: captures queries, serves deterministic rows -------------------
    property var imdbQueries: []
    function idxRow(tt, title, extra) {
        var r = { tt: tt, type: "movie", title: title, year: 2015, endYear: 0, runtimeMin: 110,
                  rating: 8.0, votes: 50000, episodes: 0, origLang: "en", isAnime: false, genres: ["Drama"] };
        for (var k in (extra || {})) r[k] = extra[k];
        return r;
    }
    function fakeImdb() {
        return {
            ready: function() { return true; },
            titleCatalog: function(query, offset, limit) {
                harness.imdbQueries.push({ query: query, offset: offset, limit: limit });
                if (query.lang === "fr") return [ harness.idxRow("tt401", "Le Film", { origLang: "fr" }) ];
                if (query.votesMax !== undefined)
                    return [ harness.idxRow("tt402", "Quiet Gem", { votes: 40000, rating: 7.9 }) ];
                return [ harness.idxRow("tt403", "Famous Classic", { votes: 3000000, rating: 9.3 }),
                         harness.idxRow("tt404", "Second Classic", { votes: 2000000, rating: 9.2 }) ];
            },
            titleFacts: function(ids) {
                // tt900 is anime, tt777 has 40 votes (shovelware), tt403 is famous
                var out = {};
                if (ids.indexOf("tt900") !== -1) out["tt900"] = { rating: 8.6, votes: 900000, isAnime: true };
                if (ids.indexOf("tt777") !== -1) out["tt777"] = { rating: 6.1, votes: 40, isAnime: false };
                if (ids.indexOf("tt403") !== -1) out["tt403"] = { rating: 9.3, votes: 3000000, isAnime: false };
                return out;
            }
        };
    }
    // live Cinemeta top fixture: one anime title + one shovelware title that must be filtered
    function liveTop() {
        function m(id, name, year) {
            return { id: id, imdb_id: id, type: "movie", name: name, poster: "p/" + id,
                     imdbRating: "7.0", releaseInfo: String(year), genres: ["Drama"] };
        }
        var out = [];
        for (var i = 0; i < 10; i++) out.push(m("tt6" + i, "Live " + i, 2024));
        out.push(m("tt900", "Sneaky Anime", 2024));
        out.push(m("tt777", "Shovelware", 2026));
        return out;
    }
    function movieAdapter(url, done) {
        if (url.indexOf("/catalog/movie/top") !== -1) { done({ metas: liveTop() }); return; }
        done({ metas: [] });
    }

    // fake source-aware explicit policy: genre "Adult" is explicit; nothing else is.
    function explicitFilter(item, showExplicit) {
        if (showExplicit) return true;
        var g = item.genres || [];
        for (var i = 0; i < g.length; i++)
            if (String(g[i]).toLowerCase() === "adult") return false;
        return true;
    }

    function rowByKey(rows, key) {
        for (var i = 0; i < rows.length; i++) if (rows[i].key === key) return rows[i];
        return null;
    }
    function itemById(items, id) {
        for (var i = 0; i < items.length; i++) if (items[i].id === id) return items[i];
        return null;
    }

    function runMoviesTests() {
        TheatreApi.resetLiveCaches();
        harness.imdbQueries = [];
        TheatreApi.setRequestAdapter(harness.movieAdapter);
        var captured = null;
        TheatreApi.loadCatalogPage("movies", { generation: 5, showExplicit: false,
                                               imdbCatalog: harness.fakeImdb(),
                                               explicitFilter: harness.explicitFilter },
                                   function(p) { if (p.generation === 5) captured = p; });
        pageAssert.captured = function() { return captured; };
        pageAssert.start();
    }
    Timer {
        id: pageAssert; interval: 250; repeat: false
        property var captured: null
        onTriggered: {
            var p = pageAssert.captured();
            harness.ok(p !== null, "movies page published");
            var rows = p.rows;
            harness.ok(rows[0].key === "top-10" && rows[0].ranked, "Top 10 first");
            // index shelves present with mapped items
            var tr = harness.rowByKey(rows, "top-rated");
            harness.ok(tr && tr.items[0].id === "tt403" && tr.items[0].imdbRating === "9.3"
                       && tr.items[0].cover.indexOf("tt403") !== -1,
                       "index row mapped: id, rating string, metahub poster");
            harness.ok(harness.rowByKey(rows, "hidden-gems").items[0].id === "tt402", "gems from band query");
            harness.ok(harness.rowByKey(rows, "french-cinema").items[0].id === "tt401", "language shelf");
            // every index query excluded anime
            harness.ok(harness.imdbQueries.length > 0
                       && harness.imdbQueries.every(function(c){ return c.query.excludeAnime === true; }),
                       "all index queries carry excludeAnime");
            // live Top 10: anime + shovelware filtered by facts, still capped at 10
            var top = harness.rowByKey(rows, "top-10");
            harness.ok(!harness.itemById(top.items, "tt900"), "anime filtered from live Top 10 via facts");
            harness.ok(top.items.length === 10, "Top 10 capped at 10");
            // recently released: shovelware (40 votes) dropped, anime dropped
            var rec = harness.rowByKey(rows, "recently-released");
            harness.ok(rec && !harness.itemById(rec.items, "tt777"), "vote-floor drops shovelware");
            harness.ok(!harness.itemById(rec.items, "tt900"), "anime dropped from recent");
            // no sub/blurb anywhere
            harness.ok(rows.every(function(r){ return r.sub === undefined && r.blurb === undefined; }), "no blurbs");
            // See-all: index pin pages the index with the offset
            harness.imdbQueries = [];
            TheatreApi.loadRowPage({ pageKey: "movies", sourceKind: "house", rowKey: "hidden-gems" },
                                   40, 40, { generation: 6, imdbCatalog: harness.fakeImdb() }, function(res) {
                harness.ok(res.generation === 6 && res.items.length > 0, "index See-all serves a page");
                harness.ok(harness.imdbQueries.length === 1 && harness.imdbQueries[0].offset === 40
                           && harness.imdbQueries[0].limit === 40, "index See-all passes offset/limit");
                TheatreApi.loadRowPage({ pageKey: "movies", sourceKind: "house", rowKey: "nope" },
                                       0, 40, { generation: 7 }, function(res2) {
                    harness.ok(res2.error.length > 0 && res2.items.length === 0, "unknown row errors honestly");
                    harness.runAnimeTests();
                });
            });
        }
    }

    // ---- anime ladder fixtures -------------------------------------------------------------
    property var malQueries: []
    function malRow(id, title) {
        return { mal_id: id, title: title, title_english: title, type: "TV", score: 8.0,
                 scored_by: 10000, members: 200000, status: "Finished Airing", year: 2015,
                 images: { jpg: { large_image_url: "l" } }, synopsis: "s", studios: [], genres: [] };
    }
    function fakeMal() {
        return {
            ready: function() { return true; },
            animeCatalog: function(query, offset, limit) {
                harness.malQueries.push({ query: query, offset: offset, limit: limit });
                return [ harness.malRow(101, "LOCAL Show"), harness.malRow(102, "LOCAL Two") ];
            }
        };
    }
    function jikanData() {
        return { data: [ { mal_id: 901, title: "JIKAN Show", type: "TV", images: { jpg: { large_image_url: "j" } }, year: 2024 },
                         { mal_id: 902, title: "JIKAN Two",  type: "TV", images: { jpg: { large_image_url: "j" } }, year: 2023 } ] };
    }
    function kitsuData() {
        return { data: [ { id: "k1", attributes: { titles: { en: "KITSU Show" }, subtype: "TV",
                                                    posterImage: { large: "k" }, startDate: "2024-05-01" } } ] };
    }
    function adapterJikanOk(url, done) {
        if (url.indexOf("api.jikan.moe") !== -1) { done(harness.jikanData()); return; }
        if (url.indexOf("kitsu.io") !== -1)      { done(harness.kitsuData()); return; }
        done(null);
    }
    function adapterJikanFailKitsuOk(url, done) {
        if (url.indexOf("api.jikan.moe") !== -1) { done(null); return; }
        if (url.indexOf("kitsu.io") !== -1)      { done(harness.kitsuData()); return; }
        done(null);
    }
    function adapterAllFail(url, done) { done(null); }
    function anyQuery(pred) {
        for (var i = 0; i < harness.malQueries.length; i++)
            if (pred(harness.malQueries[i].query)) return true;
        return false;
    }
    // anime load is fully synchronous under the harness (no deferred transport) — capture final rows.
    function runAnimeScenario(gen, malReady, adapterFn) {
        TheatreApi.resetLiveCaches();
        harness.malQueries = [];
        TheatreApi.setRequestAdapter(adapterFn);
        var mal = malReady ? harness.fakeMal() : null;
        var captured = { rows: [] };
        TheatreApi.loadCatalogPage("anime", { generation: gen, showExplicit: true, malCatalog: mal,
                                              explicitFilter: harness.explicitFilter },
                                   function(p) { if (p.generation === gen) captured = p; });
        return captured.rows;
    }
    function keyIndex(rows, key) { for (var i = 0; i < rows.length; i++) if (rows[i].key === key) return i; return -1; }

    function runAnimeTests() {
        // Scenario 1 — bundled ready + Jikan succeeds: local paints, live refreshes hot shelves.
        var s1 = harness.runAnimeScenario(11, true, harness.adapterJikanOk);
        harness.ok(s1.length > 0 && s1[0].key === "top-10", "Anime Top 10 first");
        harness.ok(harness.keyIndex(s1, "trending") === -1, "Trending omitted (no keyless trend signal)");
        var iMovies = harness.keyIndex(s1, "top-anime-movies");
        var iDecade = harness.keyIndex(s1, "2020s-anime");
        var iMecha  = harness.keyIndex(s1, "mecha");
        harness.ok(iMovies !== -1 && iDecade !== -1 && iMecha !== -1 && iMovies < iDecade && iDecade < iMecha,
                   "approved anime order preserved (movies < decades < themes)");
        // recipe -> allowlisted MAL query mapping
        harness.ok(harness.anyQuery(function(q) { return q.type === "Movie"; }), "Top Anime Movies maps to type=Movie");
        harness.ok(harness.anyQuery(function(q) { return q.yearFrom === 2010 && q.yearTo === 2019; }), "2010s maps to a decade window");
        harness.ok(harness.anyQuery(function(q) { return q.yearTo === 1999; }), "1990s-earlier maps to yearTo=1999");
        harness.ok(harness.anyQuery(function(q) { return q.tag === "Mecha"; }), "Mecha maps to an exact tag");
        harness.ok(harness.anyQuery(function(q) { return q.tag === "Horror"; })
                   && harness.anyQuery(function(q) { return q.tag === "Supernatural"; }),
                   "Horror & Supernatural fans out over both tags");
        harness.ok(harness.anyQuery(function(q) { return q.status === "Currently Airing"; }), "Airing maps to status");
        harness.ok(harness.anyQuery(function(q) { return q.status === "Not yet aired"; }), "Upcoming maps to Not yet aired");
        harness.ok(harness.anyQuery(function(q) { return q.voteFloor === 5000; }), "score shelves carry a vote floor");
        var an1 = harness.rowByKey(s1, "airing-now");
        harness.ok(an1 && an1.items[0].title === "JIKAN Show", "Jikan refreshes the airing shelf");
        var me1 = harness.rowByKey(s1, "mecha");
        harness.ok(me1 && me1.items[0].title === "LOCAL Show", "bundled-only shelf stays local under live refresh");

        // Scenario 2 — bundled ready + Jikan fails + Kitsu succeeds: airing comes from Kitsu.
        var s2 = harness.runAnimeScenario(12, true, harness.adapterJikanFailKitsuOk);
        var an2 = harness.rowByKey(s2, "airing-now");
        harness.ok(an2 && an2.items[0].title === "KITSU Show", "Kitsu refreshes airing when Jikan fails");

        // Scenario 3 — bundled ready + both live fail: nothing blanks, bundled retained.
        var s3 = harness.runAnimeScenario(13, true, harness.adapterAllFail);
        var an3 = harness.rowByKey(s3, "airing-now");
        harness.ok(an3 && an3.items[0].title === "LOCAL Show", "both live sources fail -> bundled row retained");
        harness.ok(harness.rowByKey(s3, "top-10") !== null, "no shelf blanks when live fails");

        // Scenario 4 — no bundle + Jikan succeeds: live shelves populate, bundled-only shelves omitted.
        var s4 = harness.runAnimeScenario(14, false, harness.adapterJikanOk);
        var t4 = harness.rowByKey(s4, "top-10");
        harness.ok(t4 && t4.items[0].title === "JIKAN Show", "no bundle: Jikan supplies the hot shelves");
        harness.ok(harness.keyIndex(s4, "mecha") === -1, "no bundle + no live source for a recipe -> shelf omitted");

        // Scenario 5 — no bundle + Jikan fails + Kitsu succeeds: only Kitsu-answerable shelves survive.
        var s5 = harness.runAnimeScenario(15, false, harness.adapterJikanFailKitsuOk);
        var t5 = harness.rowByKey(s5, "top-10");
        harness.ok(t5 && t5.items[0].title === "KITSU Show", "no bundle + Jikan fail: Kitsu supplies what it can");
        harness.ok(harness.keyIndex(s5, "2010s-anime") === -1, "no bundle: decade shelf has no source -> omitted");

        // See-all anime paging respects offset + the recipe's query
        harness.malQueries = [];
        TheatreApi.loadAnimeRowPage({ pageKey: "anime", sourceKind: "house", rowKey: "top-anime-movies" },
                                    24, 24, { generation: 9, malCatalog: harness.fakeMal() }, function(res) {
            harness.ok(res.generation === 9 && res.items.length > 0, "anime See-all returns a page");
            harness.ok(harness.anyQuery(function(q) { return q.type === "Movie"; }), "anime See-all uses the recipe's query");
            harness.ok(harness.malQueries.length > 0 && harness.malQueries[0].offset === 24,
                       "anime See-all passes the offset through");
            harness.finish();
        });
    }

    function finish() {
        TheatreApi.resetRequestAdapter();
        if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
        else console.log("THEATRE_API_ROWS_OK");
        Qt.exit(harness.fails.length);
    }

    Timer {
        interval: 20; running: true; repeat: false
        onTriggered: { harness.runMoviesTests(); }
    }
}
