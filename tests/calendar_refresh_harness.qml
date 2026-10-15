// Proof of CalendarApi's fetch-support helpers (the pure parts of refreshCalendar):
// stamp freshness (6h) and the release-window slice. NEVER throw; Qt.exit(fails).
import QtQuick
import "../qml/CalendarApi.js" as Api

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(c, label) { if (!c) fails.push(label); }
            var NOW = new Date(2026, 6, 23, 20, 0, 0, 0).getTime();
            var SIXH = 6 * 3600 * 1000;

            // stampFresh: within 6h = fresh; older/missing = stale
            ok(Api.stampFresh({ payload: { libCalStampAt: NOW - 1000 } }, NOW) === true, "recent stamp fresh");
            ok(Api.stampFresh({ payload: { libCalStampAt: NOW - SIXH - 1000 } }, NOW) === false, "old stamp stale");
            ok(Api.stampFresh({ payload: {} }, NOW) === false, "missing stamp stale");
            ok(Api.stampFresh({ }, NOW) === false, "no payload stale");

            // calendarSlice: keep videos in [now-45d, now+120d] with parseable released,
            // mapped to the compact shape; drop out-of-window and unparseable.
            function relIso(off) { var d = new Date(NOW); d.setDate(d.getDate()+off); d.setHours(12,0,0,0); return d.toISOString(); }
            var videos = [
                { id: "a:1:1", season: 1, episode: 1, title: "keep-past", released: relIso(-10) },
                { id: "a:1:2", season: 1, episode: 2, title: "keep-future", released: relIso(30) },
                { id: "a:1:3", season: 1, episode: 3, title: "too-old", released: relIso(-60) },
                { id: "a:1:4", season: 1, episode: 4, title: "too-far", released: relIso(200) },
                { id: "a:1:5", season: 1, episode: 5, title: "no-date", released: "" },
                { id: "a:1:6", season: 1, episode: 6, title: "junk", released: "not-a-date" }
            ];
            var slice = Api.calendarSlice(videos, NOW);
            ok(slice.length === 2, "slice keeps 2 in-window: " + slice.length);
            var titles = slice.map(function(v){ return v.title; }).sort();
            ok(titles[0] === "keep-future" && titles[1] === "keep-past", "slice titles: " + JSON.stringify(titles));
            ok(slice[0].id !== undefined && slice[0].season !== undefined && slice[0].episode !== undefined
               && slice[0].title !== undefined && slice[0].released !== undefined, "slice compact shape");
            // no extra fields beyond the compact 5
            var keys = Object.keys(slice[0]).sort();
            ok(keys.join(",") === "episode,id,released,season,title", "slice exactly 5 keys: " + keys.join(","));

            // refreshCalendar orchestration (synchronous fakes): series only, upsert-patch
            var entryA = { id: "s1", type: "series", title: "S1", cover: "", payload: { libNotif: true } };
            var entryMovie = { id: "m1", type: "movie", title: "M", payload: {} };
            var fakeMeta = { videos: [ { id: "s1:1:1", season: 1, episode: 1, title: "E1", released: relIso(5) } ] };
            var added = [], askedTypes = [];
            Api.refreshCalendar([entryA, entryMovie],
                function(type, id, cb) { askedTypes.push(type); cb(fakeMeta); },
                function(e) { added.push(e); },
                NOW,
                function() {
                    ok(added.length === 1, "refresh: only series (movie skipped): " + added.length);
                    ok(askedTypes.length === 1 && askedTypes[0] === "series", "refresh: loadMeta series only");
                    ok(added[0].payload.libCalendar.length === 1, "refresh: slice stored");
                    ok(added[0].payload.libCalStampAt === NOW, "refresh: stamp written");
                    ok(added[0].payload.libNotif === true, "refresh: existing payload preserved");
                });
            // a fresh entry is skipped (no fetch)
            var freshEntry = { id: "s2", type: "series", title: "S2", payload: { libCalStampAt: NOW - 1000 } };
            var added2 = [];
            Api.refreshCalendar([freshEntry], function(t, i, cb) { cb({ videos: [] }); }, function(e) { added2.push(e); }, NOW, function() {});
            ok(added2.length === 0, "refresh: fresh entry skipped");

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("calendar_refresh_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
