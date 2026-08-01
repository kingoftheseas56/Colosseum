// Offscreen proof of TheatreCatalogRules' pure derivations (Theatre Deep Catalogue, Task 1).
// NEVER throw inside an offscreen harness (it hangs qml.exe): collect fails, print the
// unique OK marker only when clean, single Qt.exit(fails.length). The whole body is guarded
// so a genuine JS error surfaces as a fail rather than a hang.
import QtQuick
import "../qml/TheatreCatalogRules.js" as Rules
import "../qml/AddonClient.js" as Addon

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }
            function has(rows, key) {
                return rows.some(function(r){ return r.key === key; });
            }
            try {
                // ---- Inventories: Top 10 first, genres never a row, no awards/fake-freshness ----
                var movies = Rules.defaultRows("movies");
                ok(movies.length > 10, "Movies has a deep inventory, got " + movies.length);
                ok(movies[0].key === "top-10" && movies[0].ranked, "Movies Top 10 first + ranked");
                ok(has(movies, "recently-released"), "Movies recent");
                ok(has(movies, "hidden-gems"), "Movies gems");
                ok(has(movies, "under-two-hours"), "Movies runtime shelf");
                ok(has(movies, "1970s-movies"), "Movies deep eras");
                ok(!movies.some(function(r){ return /award/i.test(r.key + r.title); }), "No awards (movies)");
                ok(!movies.some(function(r){ return /in.?theaters|coming.?soon/i.test(r.key + " " + r.title); }),
                   "No unsupported freshness (movies)");
                ok(movies.every(function(r){ return r.sub === undefined && r.blurb === undefined; }),
                   "No shelf blurb/sub field on movie rows");

                var shows = Rules.defaultRows("shows");
                ok(shows[0].key === "top-10" && shows[0].ranked, "Shows Top 10 first");
                ok(has(shows, "currently-airing"), "Shows airing");
                ok(has(shows, "long-running-series"), "Shows long-running");
                ok(has(shows, "korean-drama"), "Shows korean-drama");
                ok(!shows.some(function(r){ return /award/i.test(r.key + r.title); }), "No awards (shows)");

                var anime = Rules.defaultRows("anime");
                ok(anime[0].key === "top-10" && anime[0].ranked, "Anime Top 10 first");
                ok(has(anime, "upcoming-season"), "Anime upcoming");
                ok(has(anime, "top-anime-movies"), "Anime movies");
                ok(has(anime, "1990s-earlier"), "Anime eras");
                ok(has(anime, "mecha"), "Anime theme shelves");
                ok(!anime.some(function(r){ return /anilist|account|login|award/i.test(r.key + r.title); }),
                   "No account/award anime shelf");

                // ---- Daily rotation: same UTC day stable, next day rotates, marker present ----
                var dayA  = Rules.dailyRows(Date.UTC(2026, 7, 1), 6);
                var dayA2 = Rules.dailyRows(Date.UTC(2026, 7, 1, 22), 6);
                var dayB  = Rules.dailyRows(Date.UTC(2026, 7, 2), 6);
                ok(dayA.length === 6, "Daily count honored, got " + dayA.length);
                ok(JSON.stringify(dayA) === JSON.stringify(dayA2), "Same UTC day stable");
                ok(JSON.stringify(dayA) !== JSON.stringify(dayB), "Next day rotates");
                ok(dayA.every(function(r){ return r.rotating === true; }), "Daily marker");
                ok(dayA.every(function(r){ return r.ranked !== true; }), "Daily shelves are not ranked");
                // daily keys are distinct within a day
                var dkeys = {}; var distinct = true;
                dayA.forEach(function(r){ if (dkeys[r.key]) distinct = false; dkeys[r.key] = true; });
                ok(distinct, "Daily keys distinct within a day");

                // ---- Ranking: weighted vote floor ----
                // score-9.4 title with only 120 votes must NOT beat a score-8.4 title with 90k votes
                var topRatedItems = [
                    { id: "tt-thin",  imdbRating: "9.4", votes: 120 },
                    { id: "tt-solid", imdbRating: "8.4", votes: 90000 },
                    { id: "tt-mid",   imdbRating: "8.0", votes: 40000 }
                ];
                var tr = Rules.rankItems({ kind: "topRated", voteFloor: 5000, mean: 6.5 }, topRatedItems, 0);
                ok(tr.length === 2, "Top Rated drops the below-floor sample, got " + tr.length);
                ok(tr[0].id === "tt-solid", "Top Rated: weighted quality wins, got " + (tr[0] && tr[0].id));
                ok(!tr.some(function(x){ return x.id === "tt-thin"; }), "Top Rated excludes tiny-sample title");

                // ---- Ranking: Hidden Gems excludes the most-popular band ----
                var gemItems = [
                    { id: "tt-blockbuster", imdbRating: "8.9", votes: 1800000 }, // too popular
                    { id: "tt-gem",         imdbRating: "8.2", votes: 42000 },   // the gem
                    { id: "tt-obscure",     imdbRating: "8.6", votes: 300 }      // below quality floor
                ];
                var gems = Rules.rankItems({ kind: "hiddenGems", voteFloor: 2000, popMax: 250000, mean: 6.5 },
                                           gemItems, 0);
                ok(gems.length === 1 && gems[0].id === "tt-gem",
                   "Hidden Gems is only the mid-popularity quality title, got "
                   + gems.map(function(x){ return x.id; }).join(","));

                // ---- Missing facts never qualify a fact-dependent recipe ----
                var runtimeItems = [
                    { id: "a", runtime: "96 min" },
                    { id: "b", runtime: "" },            // missing runtime -> excluded
                    { id: "c", runtime: "142 min" }      // over two hours -> excluded
                ];
                var under2 = Rules.rankItems({ kind: "runtimeUnder", maxMinutes: 120 }, runtimeItems, 0);
                ok(under2.length === 1 && under2[0].id === "a",
                   "Under Two Hours excludes missing + over-limit, got "
                   + under2.map(function(x){ return x.id; }).join(","));

                var countryItems = [
                    { id: "jp", country: "Japan" },
                    { id: "us", country: "United States" },
                    { id: "no", country: "" }            // missing country -> excluded
                ];
                var jp = Rules.rankItems({ kind: "country", country: "Japan" }, countryItems, 0);
                ok(jp.length === 1 && jp[0].id === "jp", "Country shelf excludes missing/mismatch country");

                var statusItems = [
                    { id: "on",  status: "Currently Airing" },
                    { id: "end", status: "Ended" },
                    { id: "unk", status: "" }            // missing status -> excluded
                ];
                var airing = Rules.rankItems({ kind: "status", status: "Currently Airing" }, statusItems, 0);
                ok(airing.length === 1 && airing[0].id === "on", "Currently Airing excludes missing/wrong status");

                var decadeItems = [
                    { id: "d1", releaseInfo: "2015" },
                    { id: "d2", releaseInfo: "2009" },
                    { id: "d3", releaseInfo: "" }        // undated -> excluded
                ];
                var d2010s = Rules.rankItems({ kind: "decade", from: 2010, to: 2019 }, decadeItems, 0);
                ok(d2010s.length === 1 && d2010s[0].id === "d1", "Decade shelf excludes undated + out-of-range");

                // ---- Recently Released: newest first, undated excluded ----
                var recentItems = [
                    { id: "old", releaseInfo: "1994" },
                    { id: "new", releaseInfo: "2024" },
                    { id: "mid", releaseInfo: "2010" },
                    { id: "nd",  releaseInfo: "" }
                ];
                var recent = Rules.rankItems({ kind: "recent" }, recentItems, 0);
                ok(recent.length === 3 && recent[0].id === "new" && recent[2].id === "old",
                   "Recently Released orders newest-first and drops undated");

                // ---- Canonical dedupe within a shelf ----
                var dupItems = [
                    { id: "tt1", imdbRating: "8.0", votes: 9000 },
                    { id: "tt1", imdbRating: "8.0", votes: 9000 },
                    { id: "tt2", imdbRating: "7.9", votes: 9000 }
                ];
                var deduped = Rules.rankItems({ kind: "top", limit: 20 }, dupItems, 0);
                ok(deduped.length === 2, "Canonical id dedupe within a shelf, got " + deduped.length);

                // ---- Explicit items are removable BEFORE ranking (policy is injected upstream) ----
                var withExplicit = [
                    { id: "safe1", imdbRating: "8.0", votes: 9000 },
                    { id: "xxx",   imdbRating: "9.0", votes: 9000, explicit: true },
                    { id: "safe2", imdbRating: "7.5", votes: 9000 }
                ];
                var prefiltered = withExplicit.filter(function(it){ return !it.explicit; });
                var ranked = Rules.rankItems({ kind: "top", limit: 20 }, prefiltered, 0);
                ok(!ranked.some(function(x){ return x.id === "xxx"; }),
                   "rankItems never resurrects a pre-filtered explicit item");
                ok(ranked.length === 2, "Explicit removal leaves the safe items");

                // ---- applyCustomization: order, append, ignore-removed, hide-unless-edit, rename ----
                var srcRows = [
                    { key: "top-10", title: "Top 10" },
                    { key: "top-rated", title: "Top Rated" },
                    { key: "hidden-gems", title: "Hidden Gems" }
                ];
                var custom = {
                    order: ["top-rated", "gone-key", "top-10"], // gone-key removed -> ignored
                    hidden: ["hidden-gems"],
                    renamed: { "top-rated": "My Best Movies" }
                };
                var rest = Rules.applyCustomization(srcRows, custom, false);
                ok(rest.length === 2, "Hidden row excluded outside edit mode, got " + rest.length);
                ok(rest[0].key === "top-rated" && rest[1].key === "top-10",
                   "Saved order honored, removed key ignored, got "
                   + rest.map(function(r){ return r.key; }).join(","));
                ok(rest[0].title === "My Best Movies", "Rename applied");
                ok(srcRows[1].title === "Top Rated", "applyCustomization does not mutate source rows");

                var edit = Rules.applyCustomization(srcRows, custom, true);
                ok(has(edit, "hidden-gems"), "Hidden row visible in edit mode");
                ok(edit.filter(function(r){ return r.key === "hidden-gems"; })[0].hidden === true,
                   "Hidden row flagged hidden in edit mode");

                // new key not in saved order appends in default order
                var srcPlusNew = srcRows.concat([{ key: "all-time-greats", title: "All-Time Greats" }]);
                var appended = Rules.applyCustomization(srcPlusNew, custom, false);
                ok(appended[appended.length - 1].key === "all-time-greats",
                   "New shelf key appends in default order");

                // reset semantics: empty custom -> default order untouched
                var def = Rules.applyCustomization(srcRows, { order: [], hidden: [], renamed: {} }, false);
                ok(def[0].key === "top-10" && def[2].key === "hidden-gems", "Empty custom restores default order");

                // ---- placeExtensions: recognized service -> main; unknown -> From Your Extensions ----
                var installed = [
                    { serviceKey: "netflix", extName: "Netflix", catalogId: "nf", transportUrl: "u1", type: "movie" },
                    { serviceKey: "", extName: "Docu Hub", catalogId: "dc", transportUrl: "u2", type: "movie" }
                ];
                var placed = Rules.placeExtensions("movies", installed, movies);
                ok(placed.mainRows.some(function(r){ return r.serviceKey === "netflix"; }),
                   "Recognized service catalogue enters the main list");
                ok(placed.extensionRows.some(function(r){ return r.extName === "Docu Hub"; }),
                   "Unknown catalogue enters From Your Extensions");
                ok(!placed.mainRows.some(function(r){ return r.extName === "Docu Hub"; }),
                   "Unknown catalogue is not promoted to a contextual slot");

                // ---- End-to-end extension placement (AddonClient classify → Rules route) ----
                function extFix(id, name, catName, o) {
                    o = o || {};
                    var cat = { id: id + "-cat", type: "movie", name: catName };
                    if (o.required) cat.extra = [{ name: "genre", isRequired: true }];
                    return { id: id, enabled: o.enabled !== false, core: o.core === true,
                             transportUrl: "https://" + id + ".example/manifest.json",
                             manifest: { name: name, catalogs: [cat] } };
                }
                var installedExts = [
                    extFix("org.hbomax",   "Max",         "Max"),
                    extFix("com.netflix",  "Netflix",     "Netflix"),
                    extFix("com.appletv",  "Apple TV+",   "Apple TV+"),
                    extFix("com.disney",   "Disney+",     "Disney+"),
                    extFix("com.prime",    "Prime Video", "Prime Video"),
                    extFix("com.amc",      "AMC",         "AMC"),
                    extFix("com.fx",       "FX",          "FX"),
                    extFix("com.docuworld","Docu World",  "Documentaries"),
                    extFix("com.indiehub", "Indie Hub",   "Indie Picks"),
                    extFix("com.disabled", "Disabled Co", "Nope",   { enabled: false }),
                    extFix("com.reqextra", "Search Only", "Search", { required: true })
                ];
                var specs = Addon.theatreCatalogSpecs(installedExts, "movie");
                var placed2 = Rules.placeExtensions("movies", specs, movies);
                ok(placed2.mainRows.length === 7, "seven recognized services enter the main list, got " + placed2.mainRows.length);
                ["netflix", "hbo", "appletv", "disney", "prime", "amc", "fx"].forEach(function(k) {
                    ok(placed2.mainRows.some(function(r) { return r.serviceKey === k; }), "service " + k + " placed contextually");
                });
                ok(placed2.extensionRows.length === 2, "two unknown catalogues under From Your Extensions, got " + placed2.extensionRows.length);
                ok(placed2.extensionRows[0].extName === "Docu World" && placed2.extensionRows[1].extName === "Indie Hub",
                   "From Your Extensions keeps installed order");
                ok(!placed2.mainRows.some(function(r) { return r.extName === "Docu World"; }),
                   "unknown catalogue is not promoted to a service slot");
                ok(!placed2.extensionRows.some(function(r) { return r.serviceKey; }),
                   "a recognized service is never dumped into From Your Extensions");
                var allExtNames = placed2.mainRows.concat(placed2.extensionRows).map(function(r) { return r.extName; });
                ok(allExtNames.indexOf("Disabled Co") === -1, "disabled extension disappears");
                ok(allExtNames.indexOf("Search Only") === -1, "required-extra (search-only) catalogue disappears");

                if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
                else console.log("THEATRE_CATALOG_RULES_OK");
                Qt.exit(fails.length);
            } catch (e) {
                console.log("HARNESS EXCEPTION: " + e + "\n" + (e && e.stack ? e.stack : ""));
                Qt.exit(99);
            }
        }
    }
}
