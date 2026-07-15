// Headless harness for the universe expansion's pure logic: UniverseApi.mergeById (the
// multi-query assembly) + Universes.configFor (the curation lookup). Importing UniverseApi
// here ALSO proves its `.import "Universes.js"` line parses — a broken .import only fails
// at RUNTIME (the leading-dot lesson), which is exactly what this gate exists to catch.
// Verdict rides the exit code (try/catch → Qt.exit; console may not flush).
import QtQuick
import "../qml/UniverseApi.js" as Api
import "../qml/Universes.js" as UDB

QtObject {
    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.log("HARNESS FAIL: " + e.message); Qt.exit(2) }
    }
    function runChecks() {
        // --- mergeById: dedupes across queries, keeps arrival order, honors the cap ---
        var a = [{ id: "tt1", t: "A" }, { id: "tt2", t: "B" }]
        var m = Api.mergeById(a, [{ id: "tt2", t: "B-again" }, { id: "tt3", t: "C" }], 18)
        if (m.length !== 3) throw new Error("dedupe failed: " + m.length)
        if (m[2].id !== "tt3") throw new Error("arrival order broken: " + m[2].id)
        m = Api.mergeById(a, [{ id: "tt3" }, { id: "tt4" }], 3)
        if (m.length !== 3) throw new Error("cap not honored: " + m.length)
        if (Api.mergeById([], null, 5).length !== 0) throw new Error("null incoming must be a no-op")
        m = Api.mergeById([], [{ id: "" }, { t: "no id" }, { id: "tt9" }], 5)
        if (m.length !== 1 || m[0].id !== "tt9") throw new Error("id-less entries must be dropped")
        // input must not be mutated (responses re-merge onto the same base)
        if (a.length !== 2) throw new Error("mergeById mutated its input")

        // --- configFor: the curation lookup behind every universe page ---
        var c = UDB.configFor("DC Animated Universe")
        if (!c.eras || c.eras.length !== 3 || c.eras[0].era !== "Gotham")
            throw new Error("DCAU must carry its Timmverse timeline eras")
        c = UDB.configFor("Weekly Shonen Jump")
        if (!c.readQueries || c.readQueries.length < 8)
            throw new Error("The magazine must carry its flagship read queries")
        c = UDB.configFor("A Song of Ice and Fire")
        if (!c.seriesQueries || c.seriesQueries.indexOf("Game of Thrones") === -1)
            throw new Error("ASOIAF must search as Game of Thrones")
        c = UDB.configFor("harry potter")   // lookup is case-insensitive
        if (!c.banner || c.banner.indexOf("live.metahub.space") === -1)
            throw new Error("Harry Potter must carry a pinned-host banner")
        if (UDB.configFor("No Such Universe").banner !== undefined
            && Object.keys(UDB.configFor("No Such Universe")).length !== 0)
            throw new Error("unknown universe must yield an empty config")

        // --- the collection: every live universe, ordered and on a pinned banner host ---
        if (UDB.universes.length !== 21) throw new Error("expected 21 universes after Pokemon retirement, got " + UDB.universes.length)
        if (UDB.universes[1].name !== "Cosmere") throw new Error("Cosmere must be collection slot 2")
        var pinned = ["live.metahub.space", "s4.anilist.co", "upload.wikimedia.org", "image.tmdb.org"]
        for (var i = 0; i < UDB.universes.length; i++) {
            var u = UDB.universes[i]
            if (u.name.toLowerCase().indexOf("pok") === 0)
                throw new Error("Pokemon must be retired from the universe collection")
            if (!u.banner || !u.blurb || !u.chips || !u.chips.length)
                throw new Error(u.name + " missing banner/blurb/chips")
            var ok = pinned.some(function(h) { return u.banner.indexOf(h) !== -1 })
            if (!ok) throw new Error(u.name + " banner rides an unknown host: " + u.banner)
            if (JSON.stringify(u.chips).toLowerCase().indexOf("volume") !== -1)
                throw new Error(u.name + " chips must never say volumes (ratified)")
        }
        if (UDB.categoryFor("Marvel Cinematic Universe") !== "cinematic") throw new Error("the MCU must stay cinematic (renamed from Marvel, Hemanth 2026-07-13)")
        if (UDB.categoryFor("Dragon Ball") !== "dragonball") throw new Error("Dragon Ball must ride the bespoke Seven-Star Saga template")
        if (UDB.categoryFor("Cosmere") !== "cosmere") throw new Error("Cosmere must ride the Cognitive Atlas template")
        if (UDB.categoryFor("One Piece") !== "onepiece") throw new Error("One Piece must ride the bespoke Grand Line template")
    }
}
