// Headless harness for MagazineApi's pure registry logic — the LONG RUN form: mapEntry
// (Jikan record → page entry, English title + printed byline), mapFlagship (curated
// AniList pin → the same shape), buildRuns (the serialization chart: members cut, lane
// packing, publishing runs reach nowYear), sortBy, decadeOf, mergeDedup (resume can never
// duplicate), fmtMembers. Importing MagazineApi also proves the .pragma library parses.
// Verdict rides the exit code (try/catch → Qt.exit; a bare throw HANGS qml.exe).
import QtQuick
import "../qml/MagazineApi.js" as Mag

QtObject {
    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.log("HARNESS FAIL: " + e.message); Qt.exit(2) }
    }
    function runChecks() {
        // --- mapEntry: Jikan record shape → page entry, English title preferred ---
        var rec = { mal_id: 13, title: "One Piece", title_english: "One Piece",
                    authors: [ { name: "Oda, Eiichiro" } ],
                    images: { jpg: { image_url: "small.jpg", large_image_url: "large.jpg" } },
                    published: { prop: { from: { year: 1997 }, to: { year: null } } },
                    status: "Publishing", members: 2100000, score: 9.2, chapters: null }
        var e = Mag.mapEntry(rec)
        if (e.title !== "One Piece") throw new Error("mapEntry title")
        if (e.cover !== "large.jpg") throw new Error("mapEntry must prefer the large cover")
        if (e.fromYear !== 1997) throw new Error("mapEntry fromYear")
        if (!e.publishing) throw new Error("mapEntry publishing flag")
        if (e.author !== "Eiichiro Oda") throw new Error("byline must read given-name first, got " + e.author)
        var romaji = Mag.mapEntry({ mal_id: 1, title: "Kimetsu no Yaiba",
                                    title_english: "Demon Slayer: Kimetsu no Yaiba" })
        if (romaji.title !== "Demon Slayer: Kimetsu no Yaiba")
            throw new Error("English title must win over romaji (the manga lane speaks English)")

        // --- mapFlagship: a curated AniList pin rides the same entry shape ---
        var f = Mag.mapFlagship({ t: "Dragon Ball", a: "Akira Toriyama", al: 30042,
                                  from: 1984, to: 1995, cover: "c.jpg" })
        if (f.title !== "Dragon Ball" || f.fromYear !== 1984 || f.toYear !== 1995)
            throw new Error("mapFlagship must carry the verified dates")
        if (f.anilistId !== 30042) throw new Error("mapFlagship must carry the AniList pin")
        if (f.publishing) throw new Error("a finished flagship is not publishing")

        // --- buildRuns: the chart's strokes — members cut, then lane packing ---
        var list = [
            { title: "One Piece",   fromYear: 1997, toYear: 0,    publishing: true,  members: 700000 },
            { title: "Dragon Ball", fromYear: 1984, toYear: 1995, publishing: false, members: 180000 },
            { title: "Naruto",      fromYear: 1999, toYear: 2014, publishing: false, members: 440000 },
            { title: "Slam Dunk",   fromYear: 1990, toYear: 1996, publishing: false, members: 200000 },
            { title: "Undated",     fromYear: 0,    toYear: 0,    publishing: false, members: 999999 },
            { title: "Tiny",        fromYear: 2005, toYear: 2006, publishing: false, members: 10 }
        ]
        var built = Mag.buildRuns(list, 4, 2026)
        if (built.runs.length !== 4) throw new Error("buildRuns must cap at maxN dated runs, got " + built.runs.length)
        var titles = built.runs.map(function(r) { return r.title })
        if (titles.indexOf("Undated") !== -1) throw new Error("an undated run cannot be drawn")
        if (titles.indexOf("Tiny") !== -1) throw new Error("the members cut must drop the smallest run")
        if (built.runs[0].title !== "Dragon Ball") throw new Error("runs must order by start year")
        var op = built.runs.filter(function(r) { return r.title === "One Piece" })[0]
        if (op.endFor !== 2026) throw new Error("a publishing run must reach nowYear")
        // lane packing: Dragon Ball ends 1995, One Piece starts 1997 — exactly the 2-year
        // gap, so One Piece reuses Dragon Ball's lane; Slam Dunk (1990) overlaps Dragon
        // Ball and must take its own
        var db = built.runs[0]
        if (db.lane !== op.lane) throw new Error("a freed lane must be reused (gap >= 2y)")
        var sd = built.runs.filter(function(r) { return r.title === "Slam Dunk" })[0]
        if (sd.lane === db.lane) throw new Error("overlapping runs cannot share a lane")
        var naruto = built.runs.filter(function(r) { return r.title === "Naruto" })[0]
        if (naruto.lane !== sd.lane) throw new Error("Naruto (1999) must reuse Slam Dunk's lane (ended 1996)")
        if (built.lanes !== 2) throw new Error("this packing needs exactly 2 lanes, got " + built.lanes)

        // --- sortBy: the registry wall's three orders — never mutates the source ---
        var wall = [ { title: "Naruto", fromYear: 1999, members: 440000 },
                     { title: "Akane-banashi", fromYear: 2022, members: 40000 },
                     { title: "Bleach", fromYear: 2001, members: 429000 } ]
        if (Mag.sortBy(wall, "alpha")[0].title !== "Akane-banashi") throw new Error("alpha sort")
        if (Mag.sortBy(wall, "year")[0].title !== "Akane-banashi") throw new Error("newest sort")
        if (Mag.sortBy(wall, "members")[0].title !== "Naruto") throw new Error("members sort")
        if (wall[0].title !== "Naruto") throw new Error("sortBy must not mutate the source")

        // --- decadeOf: the decade filter's key ---
        if (Mag.decadeOf(1984) !== "1980s") throw new Error("decadeOf 1984")
        if (Mag.decadeOf(2026) !== "2020s") throw new Error("decadeOf 2026")
        if (Mag.decadeOf(0) !== "") throw new Error("undated has no decade")

        // --- mergeDedup: resuming a failed walk can never duplicate an entry ---
        var acc = [ { malId: 5, title: "One Piece" }, { malId: 6, title: "MHA" } ]
        var merged = Mag.mergeDedup(acc, [ { malId: 6, title: "MHA" }, { malId: 9, title: "Sakamoto Days" } ])
        if (merged.length !== 3) throw new Error("mergeDedup must file only new ids, got " + merged.length)
        if (merged === acc) throw new Error("mergeDedup must return a NEW array (bindings refresh)")
        if (acc.length !== 2) throw new Error("mergeDedup must not mutate the accumulator")

        // --- fmtMembers: readable MAL member counts, never invented ---
        if (Mag.fmtMembers(2143567) !== "2.1M") throw new Error("fmtMembers M: " + Mag.fmtMembers(2143567))
        if (Mag.fmtMembers(96432) !== "96k") throw new Error("fmtMembers k")
        if (Mag.fmtMembers(0) !== "") throw new Error("zero members must render as nothing")

        // --- the archive lane's cache gate starts cold ---
        if (Mag.hasPage(83, 1)) throw new Error("no archive page may sit in a cold cache")
    }
}
