// Offscreen proof of TheatreApi's deep keyless Movies/Shows engine (Task 3). A transport
// adapter feeds deterministic Cinemeta top/genre/full-meta fixtures; catalog responses are
// synchronous, full-meta responses are DEFERRED through a pump so the bounded four-worker
// enrichment concurrency is observable. NEVER throw offscreen: collect fails, print the OK
// marker only when clean, single Qt.exit(fails.length).
import QtQuick
import "../qml/TheatreApi.js" as TheatreApi

Item {
    id: harness

    // deferred full-meta transport instrumentation
    property var metaPending: []
    property int metaActive: 0
    property int metaMax: 0
    property var urlCounts: ({})

    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    // ---- fixtures --------------------------------------------------------------------------
    // top-catalog metas. tt05..tt10 arrive with NO runtime/country (force enrichment); tt09 is
    // explicit (genre Adult); tt12 stays permanently unenriched (its full-meta is null).
    function meta(id, name, rating, pop, year, genres, runtime, country) {
        return { id: id, imdb_id: id, type: "movie", name: name, poster: "p/" + id,
                 imdbRating: rating, popularity: pop, releaseInfo: String(year), year: year,
                 genres: genres, runtime: runtime, country: country };
    }
    function topMovies() {
        return [
            meta("tt01", "Alpha",   "8.8", 20,  2024, ["Action","Adventure"], "128 min", "United States"),
            meta("tt02", "Bravo",   "9.2", 2,   2016, ["Drama"],              "101 min", "France"),
            meta("tt03", "Charlie", "8.6", 18,  2012, ["Documentary"],        "88 min",  "United Kingdom"),
            meta("tt04", "Delta",   "7.9", 15,  2003, ["Comedy"],             "142 min", "Japan"),
            meta("tt05", "Echo",    "8.1", 12,  1998, ["Animation"],          "",        ""),
            meta("tt06", "Foxtrot", "6.5", 10,  2024, ["Horror"],             "",        ""),
            meta("tt07", "Golf",    "8.4", 9,   1985, ["Drama"],              "",        ""),
            meta("tt08", "Hotel",   "9.0", 17,  2020, ["Action"],             "",        ""),
            meta("tt09", "India",   "7.2", 6,   1979, ["Adult"],              "",        ""),
            meta("tt10", "Juliet",  "8.7", 14,  2015, ["Documentary"],        "",        ""),
            meta("tt11", "Kilo",    "8.3", 1.5, 2021, ["Drama"],              "112 min", "France"),
            meta("tt12", "Lima",    "7.7", 4,   2009, ["Comedy"],             "",        "")
        ];
    }
    function animationMetas() {
        return [ topMovies()[4],   // tt05 (dup, merges)
                 meta("tt14", "November", "7.6", 5, 2001, ["Animation"], "", "") ];
    }
    // full-meta fixtures (enrichment). tt12 -> null (stays missing -> never qualifies).
    function metaFixtureFor(url) {
        var map = {
            "tt05": { runtime: "120 min", country: "United States", videos: [] },
            "tt06": { runtime: "92 min",  country: "United States", videos: [] },
            "tt07": { runtime: "119 min", country: "South Korea",  videos: [] },
            "tt08": { runtime: "155 min", country: "United States", videos: [] },
            "tt09": { runtime: "99 min",  country: "Sweden",        videos: [] },
            "tt10": { runtime: "105 min", country: "Germany",       videos: [] },
            "tt14": { runtime: "98 min",  country: "Canada",        videos: [] }
        };
        for (var id in map)
            if (url.indexOf("/meta/movie/" + id) !== -1) return { meta: map[id] };
        return null;   // tt12 and anything else: no meta
    }
    function catalogFixtureFor(url) {
        if (url.indexOf("/genre=Documentary") !== -1) return null;        // simulated failed fetch
        if (url.indexOf("/genre=Animation") !== -1)   return { metas: animationMetas() };
        if (url.indexOf("/genre=") !== -1)            return { metas: [] }; // all other genres empty
        if (url.indexOf("/catalog/movie/top") !== -1) return { metas: topMovies() };
        return { metas: [] };
    }

    function adapter(url, done) {
        if (url.indexOf("/meta/") !== -1) {
            harness.metaActive += 1;
            if (harness.metaActive > harness.metaMax) harness.metaMax = harness.metaActive;
            harness.urlCounts[url] = (harness.urlCounts[url] || 0) + 1;
            harness.metaPending.push({ done: done, value: metaFixtureFor(url) });
            pump.running = true;
            return;
        }
        done(catalogFixtureFor(url));   // catalogs answer synchronously
    }

    Timer {
        id: pump; interval: 15; repeat: true; running: false
        onTriggered: {
            if (harness.metaPending.length === 0) { pump.running = false; return; }
            var batch = harness.metaPending; harness.metaPending = [];
            for (var i = 0; i < batch.length; i++) {
                harness.metaActive -= 1;
                batch[i].done(batch[i].value);
            }
        }
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
    function runtimeNum(item) { var m = String(item.runtime || "").match(/(\d+)/); return m ? parseInt(m[1]) : -1; }

    property var lastRows: []
    property var genSeen: ({})

    Timer {
        interval: 20; running: true; repeat: false
        onTriggered: {
            TheatreApi.setRequestAdapter(harness.adapter);
            var opts = { generation: 7, showExplicit: false, nowMs: Date.UTC(2026, 7, 1),
                         explicitFilter: harness.explicitFilter };
            TheatreApi.loadCatalogPage("movies", opts, function(payload) {
                harness.genSeen[payload.generation] = true;
                if (payload.generation === 7) harness.lastRows = payload.rows;
            });
            assertTimer.start();
        }
    }

    Timer {
        id: assertTimer; interval: 1200; repeat: false
        onTriggered: {
            try {
                var rows = harness.lastRows;
                harness.ok(rows.length > 0, "movies page publishes rows");

                // generation echoed on every push (page ignores stale)
                harness.ok(harness.genSeen[7] === true, "generation echoed on push");
                var otherGen = false;
                for (var gk in harness.genSeen) if (String(gk) !== "7") otherGen = true;
                harness.ok(!otherGen, "no foreign generation leaked into gen-7 pushes");

                // Top 10 first, ranked, ten items, explicit removed
                harness.ok(rows[0].key === "top-10" && rows[0].ranked, "Top 10 first + ranked");
                var top10 = rows[0].items;
                harness.ok(top10.length === 10, "Top 10 has ten items, got " + top10.length);
                harness.ok(!harness.itemById(top10, "tt09"), "explicit tt09 filtered from Top 10");

                // no shelf carries sub/blurb copy
                var clean = true;
                for (var r = 0; r < rows.length; r++)
                    if (rows[r].sub !== undefined || rows[r].blurb !== undefined) clean = false;
                harness.ok(clean, "no row carries sub/blurb shelf copy");

                // preview factual fields preserved on items
                var a = harness.itemById(top10, "tt01");
                harness.ok(a && a.imdbRating === "8.8" && a.releaseInfo === "2024"
                           && a.genres.indexOf("Action") !== -1,
                           "preview imdbRating/releaseInfo/genres preserved");

                // Recently Released / Top Rated / Hidden Gems exist and are DISTINCT
                var recent = harness.rowByKey(rows, "recently-released");
                var topRated = harness.rowByKey(rows, "top-rated");
                var gems = harness.rowByKey(rows, "hidden-gems");
                harness.ok(recent && topRated && gems, "recent + top-rated + gems all present");
                harness.ok(recent.items[0].id === "tt01", "Recently Released newest first (tt01)");
                harness.ok(topRated.items[0].id === "tt08", "Top Rated highest established rating (tt08)");
                harness.ok(gems.items[0].id === "tt02", "Hidden Gems low-pop quality first (tt02)");
                harness.ok(recent.items[0].id !== topRated.items[0].id
                           && topRated.items[0].id !== gems.items[0].id
                           && recent.items[0].id !== gems.items[0].id,
                           "the three quality shelves are distinct");

                // Under Two Hours: only enriched/preview runtimes <= 120; missing-runtime excluded
                var u2h = harness.rowByKey(rows, "under-two-hours");
                harness.ok(!!u2h, "under-two-hours present after enrichment");
                var allUnder = true;
                for (var i = 0; i < u2h.items.length; i++)
                    if (harness.runtimeNum(u2h.items[i]) > 120 || harness.runtimeNum(u2h.items[i]) < 0) allUnder = false;
                harness.ok(allUnder, "Under Two Hours contains only <=120min items");
                harness.ok(!harness.itemById(u2h.items, "tt01"), "tt01 (128min) excluded from Under Two Hours");
                harness.ok(!harness.itemById(u2h.items, "tt12"), "tt12 (never enriched) excluded from Under Two Hours");

                // Japanese Cinema: only Japan-country items
                var jp = harness.rowByKey(rows, "japanese-cinema");
                if (jp) {
                    var allJp = true;
                    for (var j = 0; j < jp.items.length; j++)
                        if (String(jp.items[j].country || "").toLowerCase().indexOf("japan") === -1) allJp = false;
                    harness.ok(allJp, "Japanese Cinema contains only Japan-country items");
                }

                // enrichment concurrency capped at four, each meta URL requested exactly once
                harness.ok(harness.metaMax === 4, "full-meta concurrency peaked at exactly 4, got " + harness.metaMax);
                var dupUrl = false;
                for (var u in harness.urlCounts) if (harness.urlCounts[u] > 1) dupUrl = true;
                harness.ok(!dupUrl, "each full-meta URL requested exactly once (coalesced/deduped)");

                // one failed genre fetch (Documentary) never blanked the successful rows
                harness.ok(rows[0].items.length === 10, "failed Documentary fetch did not blank Top 10");

                runRowPageTests();
            } catch (e) {
                harness.fails.push("ASSERT EXCEPTION: " + e);
                harness.finish();
            }
        }
    }

    property int rowPageDone: 0
    function runRowPageTests() {
        // house pin: Top Rated See-all page returns ranked items, explicit filtered, gen echoed
        var pin = { pageKey: "movies", sourceKind: "house", rowKey: "top-rated", title: "Top Rated" };
        TheatreApi.loadRowPage(pin, 0, 40, { generation: 3, showExplicit: false, nowMs: Date.UTC(2026,7,1),
                                             explicitFilter: harness.explicitFilter }, function(res) {
            harness.ok(res.generation === 3, "loadRowPage echoes generation");
            harness.ok(res.items.length > 0, "loadRowPage house pin returns items");
            harness.ok(!harness.itemById(res.items, "tt09"), "loadRowPage applies explicit filter");
            harness.ok(typeof res.hasMore === "boolean", "loadRowPage reports hasMore");
            // unknown row key: honest error, NOT silently rerouted to Top 10
            TheatreApi.loadRowPage({ pageKey: "movies", sourceKind: "house", rowKey: "does-not-exist" },
                                   0, 40, { generation: 4 }, function(res2) {
                harness.ok(res2.error && res2.error.length > 0, "unknown row key returns an error");
                harness.ok(res2.items.length === 0, "unknown row key does not reroute to Top 10");
                harness.finish();
            });
        });
    }

    function finish() {
        TheatreApi.resetRequestAdapter();
        if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
        else console.log("THEATRE_API_ROWS_OK");
        Qt.exit(harness.fails.length);
    }
}
