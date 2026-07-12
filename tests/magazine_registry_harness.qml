// Headless harness for MagazineApi's pure registry logic: mapEntry (Jikan record → page
// entry), bucketByEra (start-year decade shelves, members-ranked, empty eras vanish),
// fmtMembers. Importing MagazineApi also proves the .pragma library parses. Verdict rides
// the exit code (try/catch → Qt.exit; a bare throw HANGS qml.exe — house lesson).
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
                    images: { jpg: { image_url: "small.jpg", large_image_url: "large.jpg" } },
                    published: { prop: { from: { year: 1997 }, to: { year: null } } },
                    status: "Publishing", members: 2100000, score: 9.2, chapters: null }
        var e = Mag.mapEntry(rec)
        if (e.title !== "One Piece") throw new Error("mapEntry title")
        if (e.cover !== "large.jpg") throw new Error("mapEntry must prefer the large cover")
        if (e.fromYear !== 1997) throw new Error("mapEntry fromYear")
        if (!e.publishing) throw new Error("mapEntry publishing flag")
        var romaji = Mag.mapEntry({ mal_id: 1, title: "Kimetsu no Yaiba",
                                    title_english: "Demon Slayer: Kimetsu no Yaiba" })
        if (romaji.title !== "Demon Slayer: Kimetsu no Yaiba")
            throw new Error("English title must win over romaji (the manga lane speaks English)")
        var bare = Mag.mapEntry({ mal_id: 2, title: "Hunter x Hunter", title_english: null })
        if (bare.title !== "Hunter x Hunter") throw new Error("romaji must stand when English is null")

        // --- bucketByEra: start year decides the shelf; members rank inside; empty eras die ---
        var list = [
            { title: "Dragon Ball",  fromYear: 1984, members: 900000 },
            { title: "Slam Dunk",    fromYear: 1990, members: 500000 },
            { title: "One Piece",    fromYear: 1997, members: 2100000 },
            { title: "Naruto",       fromYear: 1999, members: 1800000 },
            { title: "MHA",          fromYear: 2014, members: 1200000 },
            { title: "Kochikame",    fromYear: 1976, members: 30000 }
        ]
        var shelves = Mag.bucketByEra(list)
        if (shelves.length !== 3) throw new Error("exactly 3 non-empty eras expected, got " + shelves.length)
        if (shelves[0].era !== "The Foundation") throw new Error("eras must stand in magazine order")
        var golden = shelves[1]
        if (golden.era !== "The Golden Age") throw new Error("1984-1999 starts belong to the Golden Age")
        if (golden.items.length !== 4) throw new Error("Golden Age must hold DB/SD/OP/Naruto, got " + golden.items.length)
        if (golden.items[0].title !== "One Piece") throw new Error("shelves must rank by members, not arrival")
        if (shelves[2].items[0].title !== "MHA") throw new Error("2014 start belongs to the New Generation")

        // --- fmtMembers: readable circulation, never invented ---
        if (Mag.fmtMembers(2143567) !== "2.1M") throw new Error("fmtMembers M: " + Mag.fmtMembers(2143567))
        if (Mag.fmtMembers(96432) !== "96k") throw new Error("fmtMembers k")
        if (Mag.fmtMembers(0) !== "") throw new Error("zero circulation must render as nothing")
    }
}
