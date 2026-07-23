// Offscreen proof of LibraryApi's pure derivations. NEVER throw (hangs offscreen);
// collect fails, Qt.exit(fails.length). Mirrors discover_api_harness's shape.
import QtQuick
import "../qml/LibraryApi.js" as Api

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            // ── watchState: the truth-order manual mark > movie-auto > episode progress ──
            ok(Api.watchState({}, { mark: 1, progress: 0, isSeries: true }) === "watched",
               "mark=1 → watched");
            ok(Api.watchState({}, { mark: 0, progress: 0.95, isSeries: false }) === "watched",
               "movie ≥0.90 auto → watched");
            ok(Api.watchState({}, { mark: -1, progress: 0.95, isSeries: false }) === "unwatched",
               "mark=-1 beats full progress → unwatched");
            ok(Api.watchState({}, { mark: 0, progress: 0.4, isSeries: true }) === "progress",
               "0<p<0.90 → progress");
            ok(Api.watchState({}, { mark: 0, progress: 0, isSeries: true }) === "unwatched",
               "no progress → unwatched");
            ok(Api.watchState({}, { mark: -1, progress: 0.4, isSeries: true }) === "progress",
               "mark=-1 mid-progress → progress");
            // ongoing series NEVER auto-completes on episode %: verbatim guard (buildRows clamps for the bar)
            ok(Api.watchState({}, { mark: 0, progress: 0.95, isSeries: true }) === "unwatched",
               "series ≥0.90 ep% with no mark → unwatched (no ongoing auto-complete)");

            // ── airingFrom ──
            ok(Api.airingFrom({ status: "Ended" }) === "ended", "status Ended → ended");
            ok(Api.airingFrom({ status: "Continuing" }) === "ongoing", "status Continuing → ongoing");
            ok(Api.airingFrom({ status: "Returning Series" }) === "ongoing", "Returning Series → ongoing");
            ok(Api.airingFrom({ releaseInfo: "2016-2022" }) === "ended", "range → ended");
            ok(Api.airingFrom({ releaseInfo: "2016-" }) === "ongoing", "open range → ongoing");
            ok(Api.airingFrom({ releaseInfo: "2020" }) === "", "single year → unknown");
            ok(Api.airingFrom({}) === "", "no info → unknown");

            // ── newEpisodeCount(videos, sinceMs, nowMs) ──
            var vids = [{ released: "2024-01-01" }, { released: "2024-06-01" },
                        { released: "2025-01-01" }, { released: "not-a-date" }];
            var now = Date.parse("2024-12-31");
            ok(Api.newEpisodeCount(vids, Date.parse("2024-03-01"), now) === 1,
               "since-latest-watched counts only newer aired");
            ok(Api.newEpisodeCount(vids, Date.parse("2023-01-01"), now) === 2,
               "since-added counts both past aired");
            ok(Api.newEpisodeCount([], Date.parse("2023-01-01"), now) === 0, "no videos → 0");
            var many = [];
            for (var i = 0; i < 120; i++) many.push({ released: "2024-06-01" });
            ok(Api.newEpisodeCount(many, Date.parse("2024-01-01"), now) === 99, "cap at 99");

            // ── finaleWatched(videos, watchedIds) — last-ordered aired ep watched ──
            var epv = [{ id: "tt:1:1", season: 1, number: 1 },
                       { id: "tt:1:2", season: 1, number: 2 },
                       { id: "tt:2:1", season: 2, number: 1 }];
            ok(Api.finaleWatched(epv, ["tt:2:1"]) === true, "finale watched → true");
            ok(Api.finaleWatched(epv, ["tt:1:2"]) === false, "non-finale watched → false");
            ok(Api.finaleWatched(epv, []) === false, "none watched → false");
            var epSpecials = [{ id: "sp", season: 0, number: 1 },
                              { id: "tt:1:1", season: 1, number: 1 },
                              { id: "fin", season: 2, number: 5 }];
            ok(Api.finaleWatched(epSpecials, ["fin"]) === true, "season0 specials don't fool finale");
            ok(Api.finaleWatched(epSpecials, ["sp"]) === false, "special watched ≠ finale");
            var epEpisodeField = [{ id: "a", season: 1, episode: 1 },
                                  { id: "b", season: 1, episode: 2 }];
            ok(Api.finaleWatched(epEpisodeField, ["b"]) === true, "episode field honored like number");

            // ── rows for applyFilters / sortRows / ledgerCounts ──
            function row(id, title, isSeries, state, progress, newCount, airing, downloaded, lastWatchedAt, year, addedAt) {
                return { entry: { id: id, title: title, type: isSeries ? "series" : "movie", addedAt: addedAt },
                    state: state, progress: progress, newCount: newCount, airing: airing,
                    downloaded: downloaded, lastWatchedAt: lastWatchedAt, year: year, isSeries: isSeries };
            }
            var r1 = row("op", "One Piece", true, "progress", 0.38, 1, "ongoing", true, 5000, 1999, 100);
            var r2 = row("sev", "Severance", true, "progress", 0.64, 2, "ongoing", true, 4000, 2022, 600);
            var r3 = row("dune", "Dune: Part Two", false, "progress", 0.31, 0, "", true, 3000, 2024, 500);
            var r4 = row("bear", "The Bear", true, "unwatched", 0, 0, "", false, 2500, 2022, 200);
            var r5 = row("monster", "Monster", true, "watched", 0, 0, "ended", false, 2000, 2004, 400);
            var r6 = row("opp", "Oppenheimer", false, "watched", 0, 0, "", false, 1000, 2023, 300);
            var rows = [r1, r2, r3, r4, r5, r6];

            // ── ledgerCounts ──
            var lc = Api.ledgerCounts(rows);
            ok(lc.saved === 6, "ledger saved=6: " + lc.saved);
            ok(lc.inProgress === 3, "ledger inProgress=3: " + lc.inProgress);
            ok(lc.unwatched === 1, "ledger unwatched=1: " + lc.unwatched);
            ok(lc.watched === 2, "ledger watched=2: " + lc.watched);
            ok(lc.newEpisodes === 2, "ledger newEpisodes=2 (series w/ new): " + lc.newEpisodes);
            ok(lc.downloaded === 3, "ledger downloaded=3: " + lc.downloaded);

            // ── applyFilters (compose with AND) ──
            function ids(rs) { return rs.map(function (r) { return r.entry.id; }).sort().join(","); }
            ok(ids(Api.applyFilters(rows, { stateFilter: "newEpisodes" })) === "op,sev",
               "filter newEpisodes: " + ids(Api.applyFilters(rows, { stateFilter: "newEpisodes" })));
            ok(ids(Api.applyFilters(rows, { stateFilter: "watched" })) === "monster,opp", "filter watched");
            ok(ids(Api.applyFilters(rows, { stateFilter: "inProgress" })) === "dune,op,sev", "filter inProgress");
            ok(ids(Api.applyFilters(rows, { stateFilter: "unwatched" })) === "bear", "filter unwatched");
            ok(ids(Api.applyFilters(rows, { stateFilter: "downloaded" })) === "dune,op,sev", "filter downloaded");
            ok(ids(Api.applyFilters(rows, { typeFilter: "movie" })) === "dune,opp", "filter movies");
            ok(ids(Api.applyFilters(rows, { typeFilter: "series" })) === "bear,monster,op,sev", "filter series");
            ok(ids(Api.applyFilters(rows, { airingFilter: "ended" })) === "monster", "filter ended");
            ok(ids(Api.applyFilters(rows, { airingFilter: "ongoing" })) === "op,sev", "filter ongoing");
            ok(ids(Api.applyFilters(rows, { query: "one" })) === "op", "search 'one' (ci)");
            ok(ids(Api.applyFilters(rows, { stateFilter: "inProgress", typeFilter: "series" })) === "op,sev",
               "compose inProgress+series");
            ok(ids(Api.applyFilters(rows, { typeFilter: "series", airingFilter: "ongoing", query: "sev" })) === "sev",
               "compose series+ongoing+query");
            ok(ids(Api.applyFilters(rows, {})) === "bear,dune,monster,op,opp,sev", "empty filter = all");

            // ── sortRows ──
            var byWatched = Api.sortRows(rows, "lastWatched");
            ok(byWatched[0].entry.id === "op" && byWatched[5].entry.id === "opp",
               "sort lastWatched desc: " + byWatched.map(function (r) { return r.entry.id; }).join(","));
            var byAdded = Api.sortRows(rows, "added");
            ok(byAdded[0].entry.id === "sev" && byAdded[5].entry.id === "op", "sort added desc by addedAt");
            var byAz = Api.sortRows(rows, "az");
            ok(byAz[0].entry.title === "Dune: Part Two" && byAz[2].entry.title === "One Piece", "sort A–Z");
            var byYear = Api.sortRows(rows, "year");
            ok(byYear[0].year === 2024 && byYear[5].year === 1999, "sort year desc");
            // sortRows must not mutate the input order
            ok(rows[0].entry.id === "op", "sortRows returns a copy (no mutation)");

            // ── buildRows join ──
            function ent(id, title, type, payload, addedAt) {
                return { id: id, title: title, type: type, cover: "", payload: payload, world: "theatre", addedAt: addedAt };
            }
            var e1 = ent("tt1", "Show One", "series", { libNewCount: 3, libAiring: "ongoing", libYear: 2020 }, 1000);
            var e2 = ent("tt2", "Show Two", "series", { libNewCount: 5, libAiring: "ended", libNotif: false, libYear: 2004 }, 900);
            var e3 = ent("tt3", "Film Three", "movie", {}, 800);
            var e4 = ent("tt4", "Fresh Series", "series", {}, 700);
            var e5 = ent("tt5", "Caught Up Show", "series", { libAiring: "ongoing" }, 600);
            var bentries = [e1, e2, e3, e4, e5];
            var plist = [
                { id: "tt1:2:9", progress: 0.5, updatedAt: 5000, watched: false, sub: "S2 · E9" },
                { id: "tt3", progress: 0.95, updatedAt: 4000 },
                { id: "tt5:1:12", progress: 0.95, updatedAt: 6000, watched: true }
            ];
            var mark0 = function (id) { return 0; };
            var brows = Api.buildRows(bentries, plist, mark0, ["tt1"], now);

            // join correctness: series in progress, movie auto-watched, stamps + downloaded joined
            ok(brows[0].state === "progress" && brows[0].progress === 0.5 && brows[0].lastWatchedAt === 5000
               && brows[0].downloaded === true && brows[0].newCount === 3 && brows[0].airing === "ongoing"
               && brows[0].year === 2020 && brows[0].isSeries === true && brows[0].sub === "S2 · E9",
               "buildRows join series (incl. resume sub)");
            ok(brows[2].state === "watched" && brows[2].isSeries === false, "buildRows movie auto-watched via matched progress");
            // notif-off suppression: libNotif===false forces newCount to 0 despite libNewCount:5
            ok(brows[1].newCount === 0 && brows[1].airing === "ended" && brows[1].state === "unwatched"
               && brows[1].lastWatchedAt === 900, "buildRows notif-off suppresses newCount + addedAt fallback");
            // stamp fallback: fresh series, no stamps and no progress → zeros/empties
            ok(brows[3].newCount === 0 && brows[3].airing === "" && brows[3].state === "unwatched"
               && brows[3].downloaded === false, "buildRows stamp fallback when no live meta yet");
            // series clamp: caught-up ongoing show keeps RAW progress for the bar but reads "in progress"
            ok(brows[4].progress === 0.95 && brows[4].state === "progress" && brows[4].lastWatchedAt === 6000,
               "buildRows series clamp: raw bar progress, in-progress state");

            // ── shouldAutoComplete (spec §4.3) — the pure decision ──
            var acVids = [ { id: "s1:1:1", season: 1, number: 1 }, { id: "s1:2:1", season: 2, number: 1 } ];
            ok(Api.shouldAutoComplete("ended", acVids, ["s1:2:1"], 0) === true, "ended+finale+no-mark → fires");
            ok(Api.shouldAutoComplete("ongoing", acVids, ["s1:2:1"], 0) === false, "ongoing NEVER auto-completes");
            ok(Api.shouldAutoComplete("ended", acVids, ["s1:2:1"], -1) === false, "manual -1 blocks auto-complete");
            ok(Api.shouldAutoComplete("ended", acVids, ["s1:2:1"], 1) === false, "already marked → no re-fire");
            ok(Api.shouldAutoComplete("ended", acVids, ["s1:1:1"], 0) === false, "non-finale watched → no fire");

            // ── refreshStamps orchestration (fakes: serial walk, skips, auto-complete, order) ──
            function fakeLoadMeta(kind, id, cb) {
                if (id === "s1") cb({ status: "Ended", videos: [
                    { id: "s1:1:1", season: 1, number: 1, released: "2020-01-01" },
                    { id: "s1:1:2", season: 1, number: 2, released: "2020-01-08" },
                    { id: "s1:2:1", season: 2, number: 1, released: "2020-02-01" } ] });
                else if (id === "s2") cb({ status: "Continuing", videos: [
                    { id: "s2:1:1", season: 1, number: 1, released: "2024-01-01" },
                    { id: "s2:1:2", season: 1, number: 2, released: "2024-06-01" },
                    { id: "s2:1:3", season: 1, number: 3, released: "2024-11-01" } ] });
                else cb(null);
            }
            var s1 = ent("s1", "Ended Show", "series", {}, 1000);
            var s2 = ent("s2", "Ongoing Show", "series", {}, 1000);
            var s3 = ent("s3", "Fresh Show", "series", { libStampAt: now, libAiring: "ongoing" }, 1000);
            var m1 = ent("m1", "A Movie", "movie", {}, 1000);
            var plist2 = [
                { id: "s1:2:1", watched: true, progress: 0.95, updatedAt: 5000 },   // s1 rep = finale, watched
                { id: "s2:1:1", watched: true, progress: 0.95, updatedAt: 4000 }    // s2 rep = ep1 (2024-01-01)
            ];
            var seq = [], addCalls = [], setMarks = [], forgets = [], doneFlag = { v: false };
            Api.refreshStamps([s1, s2, s3, m1], fakeLoadMeta, plist2,
                function (id) { return 0; },
                function (id) { forgets.push(id); seq.push("forget:" + id); },
                function (id, on) { setMarks.push(id + ":" + on); seq.push("setmark:" + id + ":" + on); },
                function (e) { addCalls.push(e); },
                now, function () { doneFlag.v = true; });

            ok(doneFlag.v === true, "refreshStamps calls done()");
            ok(addCalls.length === 2, "refreshStamps re-stamps only stale series (s1,s2): " + addCalls.length);
            var byId = {};
            for (var ai = 0; ai < addCalls.length; ai++) byId[addCalls[ai].id] = addCalls[ai].payload;
            ok(byId["s1"] && byId["s1"].libAiring === "ended" && byId["s1"].libNewCount === 0
               && byId["s1"].libStampAt === now, "s1 stamped ended + auto-completed (newCount 0)");
            ok(byId["s2"] && byId["s2"].libAiring === "ongoing" && byId["s2"].libNewCount === 2
               && byId["s2"].libStampAt === now, "s2 stamped ongoing + 2 new since watched ep");
            ok(!byId["s3"] && !byId["m1"], "fresh series + movie skipped (no fetch/re-stamp)");
            ok(setMarks.length === 1 && setMarks[0] === "s1:true", "only s1 auto-marks watched");
            ok(forgets.length === 1 && forgets[0] === "s1", "s1 forgotten before marking");
            ok(seq.indexOf("forget:s1") < seq.indexOf("setmark:s1:true"), "forget precedes setmark (order matters)");

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("library_api_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
