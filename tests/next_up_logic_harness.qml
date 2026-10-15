// Headless behavioral harness for the Next Up derivations (spec 2026-07-18,
// Jellyfin library inheritance: Theatre episodes + Tankoban manga units).
// Verdict rides the EXIT CODE — Qt.exit(0) pass, non-zero fail — because console
// output is not guaranteed to flush and an uncaught onCompleted throw HANGS qml.exe.
import QtQuick
import "../qml/NextUp.js" as NU

QtObject {
    Component.onCompleted: {
        try {
            runChecks()
            Qt.exit(0)
        } catch (e) {
            console.log("HARNESS FAIL: " + e.message)
            Qt.exit(2)
        }
    }

    function runChecks() {
        // --- finishedShows: latest-per-show wins, finished gate, movies never card ---
        var entries = [
            { "id": "tt1:2:5", "title": "Show A - S2E5", "watched": true, "progress": 1 },
            { "id": "tt1:2:4", "title": "Show A - S2E4", "watched": true, "progress": 1 },   // older, same show
            { "id": "tt2:1:3", "title": "Show B - S1E3", "progress": 0.45 },                 // unfinished latest
            { "id": "tt3", "title": "A Movie", "watched": true, "progress": 1 },             // movie
            { "id": "tt4:1:1", "title": "Show C - S1E1", "progress": 0.93 }                  // 0.90 line, no flag
        ]
        var fin = NU.finishedShows(entries)
        if (fin.length !== 2)
            throw new Error("expected 2 finished shows, got " + fin.length)
        if (fin[0].show !== "tt1" || fin[0].entry.id !== "tt1:2:5")
            throw new Error("latest entry per show must win")
        if (fin[1].show !== "tt4")
            throw new Error("the 0.90 progress line must count as finished")
        // Show B's latest is unfinished: it belongs to Continue, never Next Up.

        // --- nextEpisodeFromMeta: same season, boundary, S0 skip, caught up ---
        var videos = [
            { "id": "tt1:0:1", "season": 0, "episode": 1, "name": "Special" },
            { "id": "tt1:1:1", "season": 1, "episode": 1, "name": "Pilot" },
            { "id": "tt1:1:2", "season": 1, "episode": 2, "name": "Two" },
            { "id": "tt1:2:1", "season": 2, "episode": 1, "name": "Opener" }
        ]
        var n = NU.nextEpisodeFromMeta(videos, "tt1:1:1")
        if (!n || n.id !== "tt1:1:2")
            throw new Error("same-season next episode expected")
        n = NU.nextEpisodeFromMeta(videos, "tt1:1:2")
        if (!n || n.id !== "tt1:2:1")
            throw new Error("season boundary must roll to the next season's first episode")
        n = NU.nextEpisodeFromMeta(videos, "tt1:2:1")
        if (n !== null)
            throw new Error("caught up (S0 skipped) must yield null, got " + JSON.stringify(n))

        // --- theatreCard: show name stripped of the SxEy suffix, fresh progress ---
        var card = NU.theatreCard(fin[0], { "id": "tt1:2:6", "season": 2, "num": 6, "title": "Six" })
        if (card.title !== "Show A")
            throw new Error("card title must be the bare show name, got '" + card.title + "'")
        if (card.progress !== 0 || card.nextUp !== true)
            throw new Error("a Next Up card is fresh (progress 0, nextUp flagged)")
        if (card.sub.indexOf("S2 · E6") !== 0)
            throw new Error("card sub must lead with SxEy, got '" + card.sub + "'")

        // --- finishedReads: per-series latest across the two manga kinds ---
        var manga = [
            { "id": "s1", "kind": "manga", "sub": "Chapter 12", "progress": 1, "watched": true },
            { "id": "s1", "kind": "manga", "sub": "Chapter 11", "progress": 1 },
            { "id": "s2", "kind": "manga", "sub": "Chapter 3", "progress": 0.4 }
        ]
        var tanko = [ { "id": "s3", "kind": "tankoban", "sub": "Vol. 2", "progress": 0.95 } ]
        var reads = NU.finishedReads(manga, tanko)
        if (reads.length !== 2 || reads[0].id !== "s1" || reads[1].id !== "s3")
            throw new Error("finishedReads must keep s1 latest + s3, drop unfinished s2")

        // --- unitNumber / nextUnit: strict-greater, decimals, nothing-later ---
        if (NU.unitNumber("Chapter 112.5") !== 112.5 || NU.unitNumber("Vol. 3") !== 3)
            throw new Error("unitNumber must parse the first float")
        if (!isNaN(NU.unitNumber("Extra")))
            throw new Error("a label with no number must parse NaN")
        var units = [
            { "id": "c10", "label": "Chapter 10" },
            { "id": "c12", "label": "Chapter 12" },
            { "id": "c12b", "label": "Chapter 12.5" }
        ]
        var u = NU.nextUnit("Chapter 10", units)
        if (!u || u.id !== "c12")
            throw new Error("next unit must be the smallest strictly greater number")
        u = NU.nextUnit("Chapter 12", units)
        if (!u || u.id !== "c12b")
            throw new Error("a decimal in-between chapter is a real next unit")
        if (NU.nextUnit("Chapter 12.5", units) !== null)
            throw new Error("nothing later on disk must yield null")
        if (NU.nextUnit("Oneshot", units) !== null)
            throw new Error("an unnumbered finished label cannot derive a next unit")
        // explicit number field (volume rows) outranks label parsing
        u = NU.nextUnit("Vol. 2", [ { "id": "v3", "label": "Vol. 3", "number": 3 } ])
        if (!u || u.id !== "v3")
            throw new Error("volume rows with explicit numbers must resolve")

        // --- mangaCard: not-downloaded card is marked and says so ---
        var mc = NU.mangaCard(reads[0], { "id": "", "label": "Next chapter", "number": NaN }, false)
        if (mc.resume.downloaded !== false || mc.sub.indexOf("not downloaded") < 0)
            throw new Error("a go-get card must carry the not-downloaded mark")

        console.log("next_up_logic_harness: all checks passed")
    }
}
