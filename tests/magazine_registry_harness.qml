// Headless harness for MagazineApi's pure registry logic — the Editorial Archive form:
// mapEntry (Jikan record → page entry, English title + printed byline), bucketByEra (the
// four APPROVED era volumes, fixed order, exact inclusive boundaries, members-ranked
// inside, empty volumes SURVIVE), undatedOf, sortEra (the volume sort switch), mergeDedup
// (resume can never duplicate), alphaSort, fmtMembers. Importing MagazineApi also proves
// the .pragma library parses. Verdict rides the exit code (try/catch → Qt.exit; a bare
// throw HANGS qml.exe — house lesson).
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
        var bare = Mag.mapEntry({ mal_id: 2, title: "Hunter x Hunter", title_english: null })
        if (bare.title !== "Hunter x Hunter") throw new Error("romaji must stand when English is null")

        // --- bucketByEra: the four approved volumes, fixed order, EXACT boundaries ---
        var list = [
            { malId: 1, title: "Kochikame",   fromYear: 1976, members: 30000 },
            { malId: 2, title: "Dragon Ball", fromYear: 1984, members: 900000 },
            { malId: 3, title: "Slam Dunk",   fromYear: 1990, members: 500000 },
            { malId: 4, title: "Edge-1996",   fromYear: 1996, members: 10 },
            { malId: 5, title: "One Piece",   fromYear: 1997, members: 2100000 },
            { malId: 6, title: "MHA",         fromYear: 2014, members: 1200000 },
            { malId: 7, title: "Demon Slayer",fromYear: 2016, members: 1500000 },
            { malId: 8, title: "Undated",     fromYear: 0,    members: 5 }
        ]
        var vols = Mag.bucketByEra(list)
        if (vols.length !== 4) throw new Error("ALL four volumes must stand, got " + vols.length)
        if (vols[0].era !== "The Founding Years" || vols[1].era !== "The Golden Age"
            || vols[2].era !== "The Big Three Era" || vols[3].era !== "The New Generation")
            throw new Error("volumes must keep the approved fixed order")
        if (vols[0].items.length !== 1 || vols[0].items[0].title !== "Kochikame")
            throw new Error("a 1976 start belongs to the Founding Years")
        if (vols[1].items.length !== 3) throw new Error("1980–1996 must hold DB/SD/Edge-1996, got " + vols[1].items.length)
        if (vols[1].items[vols[1].items.length - 1].title !== "Edge-1996")
            throw new Error("1996 is the Golden Age's INCLUSIVE upper edge")
        if (vols[2].items[0].title !== "One Piece") throw new Error("1997 opens the Big Three Era")
        if (vols[2].items.indexOf(vols[2].items.filter(function(m){ return m.title === "MHA" })[0]) < 0)
            throw new Error("a 2014 start still belongs to the Big Three Era")
        if (vols[3].items.length !== 1 || vols[3].items[0].title !== "Demon Slayer")
            throw new Error("2015+ starts belong to the New Generation")
        if (vols[1].items[0].title !== "Dragon Ball") throw new Error("volumes rank by MAL members inside")
        var undated = Mag.undatedOf(list)
        if (undated.length !== 1 || undated[0].title !== "Undated")
            throw new Error("undated entries belong to no volume — the index carries them")

        // --- sortEra: the volume's sort switch — never mutates the source ---
        var golden = vols[1].items
        var chrono = Mag.sortEra(golden, "year")
        if (chrono[0].title !== "Dragon Ball" || chrono[2].title !== "Edge-1996")
            throw new Error("chronological sort must ride the real start years")
        if (golden[0].title !== "Dragon Ball") throw new Error("sortEra must not mutate the volume")
        var byMembers = Mag.sortEra(chrono, "members")
        if (byMembers[0].title !== "Dragon Ball") throw new Error("members sort must rank by MAL members")

        // --- mergeDedup: resuming a failed walk can never duplicate an entry ---
        var acc = [ { malId: 5, title: "One Piece" }, { malId: 6, title: "MHA" } ]
        var merged = Mag.mergeDedup(acc, [ { malId: 6, title: "MHA" }, { malId: 9, title: "Sakamoto Days" } ])
        if (merged.length !== 3) throw new Error("mergeDedup must file only new ids, got " + merged.length)
        if (merged === acc) throw new Error("mergeDedup must return a NEW array (bindings refresh)")
        if (acc.length !== 2) throw new Error("mergeDedup must not mutate the accumulator")

        // --- alphaSort: the complete registry index ---
        var alpha = Mag.alphaSort([ { title: "Naruto" }, { title: "Bleach" }, { title: "Akane-banashi" } ])
        if (alpha[0].title !== "Akane-banashi" || alpha[2].title !== "Naruto")
            throw new Error("alphaSort must order the index alphabetically")

        // --- fmtMembers: readable MAL member counts, never invented ---
        if (Mag.fmtMembers(2143567) !== "2.1M") throw new Error("fmtMembers M: " + Mag.fmtMembers(2143567))
        if (Mag.fmtMembers(96432) !== "96k") throw new Error("fmtMembers k")
        if (Mag.fmtMembers(0) !== "") throw new Error("zero members must render as nothing")

        // --- the archive lane's cache gate starts cold ---
        if (Mag.hasPage(83, 1)) throw new Error("no archive page may sit in a cold cache")
    }
}
