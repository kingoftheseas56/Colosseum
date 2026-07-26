// Offscreen proof of CalendarApi's pure derivations (Stage 3, spec §5).
// NEVER throw (hangs offscreen); collect fails, Qt.exit(fails.length).
// Timezone-robust: fixtures are anchored to NOW's LOCAL calendar day at safe
// mid-day hours, so day labels are deterministic on any machine (this box is IST).
import QtQuick
import "../qml/CalendarApi.js" as Api

Item {
    Timer {
        interval: 10; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(c, label) { if (!c) fails.push(label); }

            var WD = ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"];
            var nowDate = new Date(2026, 6, 23, 20, 0, 0, 0);   // local: 2026-07-23 20:00
            var NOW = nowDate.getTime();
            function atDay(off, hour) { var d = new Date(NOW); d.setDate(d.getDate()+off); d.setHours(hour,0,0,0); return d; }
            function relIso(off, hour) { return atDay(off, hour).toISOString(); }

            var onePiece = { id: "tt0388629", type: "series", title: "One Piece", cover: "op.jpg",
                addedAt: NOW - 100000000, payload: { libCalendar: [
                    { id: "tt0388629:21:1", season: 21, episode: 1, title: "Return", released: relIso(-1, 15) }, // yesterday, watched
                    { id: "tt0388629:21:2", season: 21, episode: 2, title: "Dawn",   released: relIso(0, 15) },  // earlier today, aired
                    { id: "tt0388629:21:3", season: 21, episode: 3, title: "Storm",  released: relIso(1, 15) },  // tomorrow (within 48h)
                    { id: "tt0388629:21:4", season: 21, episode: 4, title: "Peak",   released: relIso(7, 15) }   // +7d, same weekday
                ] } };
            var silenced = { id: "tt_sil", type: "series", title: "Muted", cover: "", addedAt: NOW,
                payload: { libNotif: false, libCalendar: [ { id: "tt_sil:1:1", season: 1, episode: 1, title: "x", released: relIso(1, 12) } ] } };
            var movie = { id: "tt_mov", type: "movie", title: "Film", cover: "", addedAt: NOW, payload: {} };
            var entries = [onePiece, silenced, movie];
            var watched = { "tt0388629:21:1": true };
            var ep4wd = WD[atDay(7, 15).getDay()];

            // chipState
            ok(Api.chipState(onePiece.payload.libCalendar[0], watched, NOW) === "watched-skip", "ep1 watched => watched-skip");
            ok(Api.chipState(onePiece.payload.libCalendar[1], watched, NOW) === "aired", "ep2 => aired");
            ok(Api.chipState(onePiece.payload.libCalendar[3], watched, NOW) === "future", "ep4 => future");

            // comingUp: ep1 dropped; ep2 aired, ep3 tomorrow, ep4 weekday; silenced+movie gone
            var up = Api.comingUp(entries, NOW, 20, watched);
            ok(up.length === 3, "comingUp count (ep2,3,4): " + up.length);
            ok(up[0].episode === 2 && up[0].state === "aired" && up[0].dayLabel === "Today", "coming[0]=ep2 aired Today: " + (up[0]&&up[0].dayLabel));
            ok(up[1].episode === 3 && up[1].state === "future" && up[1].dayLabel === "Tomorrow", "coming[1]=ep3 Tomorrow");
            ok(up[2].episode === 4 && up[2].dayLabel === ep4wd, "coming[2]=ep4 weekday " + ep4wd + " got " + (up[2]&&up[2].dayLabel));
            for (var i = 0; i < up.length; i++) ok(up[i].seriesId === "tt0388629", "no silenced/movie in comingUp");

            // dayLabel far
            ok(Api.dayLabel(atDay(23, 10).getTime(), NOW).indexOf("Aug") === 0, "far label month-day: " + Api.dayLabel(atDay(23,10).getTime(), NOW));

            // livingLine: nearest FUTURE airing within 48h = ep3 (tomorrow) → 1 series
            var line = Api.livingLine(entries, NOW);
            ok(line && line.count === 1, "livingLine 1 series w/in 48h: " + JSON.stringify(line));
            ok(line && /1 of yours airs tomorrow/.test(line.label), "livingLine label: " + (line && line.label));
            ok(Api.livingLine([silenced, movie], NOW) === "", "livingLine empty when nothing qualifies");

            // monthGrid July: ep2,ep3,ep4 chips (ep1 watched excluded, silenced excluded)
            var grid = Api.monthGrid(entries, 2026, 7, NOW, watched);
            ok(grid && grid.weeks && grid.weeks.length >= 4, "grid weeks present");
            var chipDays = 0, sawSilenced = false, todayCells = 0;
            for (var w = 0; w < grid.weeks.length; w++) for (var d = 0; d < grid.weeks[w].length; d++) {
                var cell = grid.weeks[w][d];
                if (cell.isToday) todayCells++;
                if (cell.chips) { chipDays += cell.chips.length;
                    for (var c = 0; c < cell.chips.length; c++) if (cell.chips[c].seriesId === "tt_sil") sawSilenced = true; }
            }
            ok(chipDays === 3, "grid chips = ep2,3,4: " + chipDays);
            ok(!sawSilenced, "no silenced chip in grid");
            ok(todayCells === 1, "exactly one isToday cell: " + todayCells);

            // dayList tomorrow → ep3
            var dl = Api.dayList(entries, atDay(1, 12).getTime(), watched);
            ok(dl.length === 1 && dl[0].episode === 3, "dayList tomorrow => ep3: " + JSON.stringify(dl));

            // monthCount July = 3
            ok(Api.monthCount(entries, 2026, 7, NOW, watched) === 3, "monthCount July: " + Api.monthCount(entries, 2026, 7, NOW, watched));

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("calendar_api_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
