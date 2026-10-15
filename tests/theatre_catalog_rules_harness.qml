// Offscreen proof of TheatreCatalogRules' pure derivations (Theatre Deep Catalogue).
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
                // ---- Ratified 2026-08-02 inventories ----
                var movies = Rules.defaultRows("movies");
                ok(movies[0].key === "top-10" && movies[0].ranked, "Movies Top 10 first");
                ["recently-released","top-rated","hidden-gems","cult-classics","under-two-hours",
                 "documentary-movies","animated-movies","international-cinema","japanese-cinema",
                 "korean-cinema","french-cinema","2020s-movies","1970s-movies"]
                    .forEach(function(k){ ok(has(movies, k), "movies has " + k); });
                ok(!has(movies, "all-time-greats"), "All-Time Greats retired");
                ok(!movies.some(function(r){ return /award|in.?theaters|coming.?soon/i.test(r.key + r.title); }),
                   "no awards or fabricated freshness");

                var shows = Rules.defaultRows("shows");
                ["top-10","currently-airing","recently-premiered","top-rated","hidden-gems","cult-classics",
                 "long-running-series","limited-series","drama-series","comedy-series","crime-and-mystery",
                 "science-fiction-and-fantasy","documentary-series","animated-series","korean-drama"]
                    .forEach(function(k){ ok(has(shows, k), "shows has " + k); });
                ok(!has(shows, "british-television"), "British Television dropped");
                ok(!has(shows, "all-time-great-series"), "All-Time Great Series retired");

                // ---- Thresholds are dials with sane relationships (never pin exact values) ----
                var T = Rules.THRESHOLDS;
                ok(T.movie.HG_VOTES_MAX < T.movie.TR_VOTES, "movie gems band sits below the top-rated floor");
                ok(T.series.HG_VOTES_MAX < T.series.TR_VOTES, "series gems band below top-rated floor");
                ok(T.movie.HG_VOTES_MIN > 0 && T.movie.CC_VOTES_MIN > 0, "bands have lower edges");

                // ---- indexQueryFor: pure recipe -> allowlisted query ----
                function q(key, page) {
                    var defs = Rules.defaultRows(page || "movies");
                    for (var i = 0; i < defs.length; i++) if (defs[i].key === key) return Rules.indexQueryFor(defs[i].recipe);
                    return undefined;
                }
                ok(q("top-10") === null, "live recipes map to null (no index query)");
                ok(q("recently-released") === null, "recently-released stays live");
                ok(q("currently-airing", "shows") === null, "currently-airing stays live");
                var tr = q("top-rated");
                ok(tr.type === "movie" && tr.order === "rating" && tr.ratingMin === T.movie.TR_RATING
                   && tr.votesMin === T.movie.TR_VOTES && tr.excludeAnime === true, "top rated query");
                var hg = q("hidden-gems");
                ok(hg.votesMin === T.movie.HG_VOTES_MIN && hg.votesMax === T.movie.HG_VOTES_MAX, "gems band");
                var cc = q("cult-classics");
                ok(cc.yearTo === 1999 && cc.votesMax === T.movie.CC_VOTES_MAX, "cult classics pre-2000 band");
                ok(q("under-two-hours").runtimeMax === 120, "runtime query");
                ok(q("animated-movies").genre === "Animation" && q("animated-movies").excludeAnime === true,
                   "animation minus anime");
                ok(q("international-cinema").notLang === "en", "international = non-english");
                ok(q("korean-cinema").lang === "ko" && q("japanese-cinema").lang === "ja"
                   && q("french-cinema").lang === "fr", "language shelves");
                ok(q("2010s-movies").yearFrom === 2010 && q("2010s-movies").yearTo === 2019, "decade window");
                ok(q("limited-series", "shows").type === "mini", "limited series is exact mini type");
                ok(q("long-running-series", "shows").order === "episodes", "long-running by episodes");
                ok(q("korean-drama", "shows").lang === "ko" && q("korean-drama", "shows").type === "series",
                   "korean drama is language-based");
                // genreAny recipes fan out client-side: mapping returns per-genre queries
                var cm = Rules.indexQueriesFor({ kind: "imdbGenreAny", genres: ["Crime","Mystery"], type: "series" });
                ok(cm.length === 2 && cm[0].genre === "Crime" && cm[1].genre === "Mystery", "genreAny fans out");
                // every index query excludes anime
                ["top-rated","hidden-gems","cult-classics","animated-movies","korean-cinema"].forEach(function(k){
                    ok(q(k).excludeAnime === true, k + " excludes anime");
                });

                // ---- Anime inventory unchanged ----
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

                // ---- daily rotation pool includes the language guests, no Holiday recipe ----
                var week = {};
                for (var d = 0; d < 14; d++)
                    Rules.dailyRows(Date.UTC(2026, 7, 1 + d), 6).forEach(function(r){ week[r.key] = r.recipe; });
                ok(Object.keys(week).some(function(k){ return /daily-(spanish|italian|german|swedish|danish)/.test(k); }),
                   "language guests rotate in across two weeks");
                ok(!Object.keys(week).some(function(k){ return /holiday/.test(k); }),
                   "no Holiday recipe (IMDb has no honest signal)");

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
