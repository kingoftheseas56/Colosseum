import QtQuick
import "../qml/WorldSearch.js" as WorldSearch

// WorldSearch comics-lane probe (spec 2026-07-18): the catalogue SQLite engine is
// the ONLY comics lane. C++ owns matching/ranking (engine harness proves that);
// this probe proves the JS mapping + merge: year-decorated cards, pure routing
// title, gcd routing data, 30-row ask, engine-absent degrade, manga merge.
Item {
    Component.onCompleted: {
        try {
            var askedLimit = -1
            var fakeEngine = {
                ready: function() { return true },
                search: function(q, limit) {
                    askedLimit = limit
                    return [
                        { gcdId: 3, title: "Justice League", year: 0,    publisher: "DC", cover: "c3", downloads: 9 },
                        { gcdId: 6, title: "Justice League", year: 2016, publisher: "DC", cover: "c6", downloads: 5 },
                        { gcdId: 7, title: "Justice League", year: 2018, publisher: "DC", cover: "",   downloads: 2 }
                    ]
                }
            }

            var hits = WorldSearch.searchCatalogDb("justice league", fakeEngine)
            if (askedLimit !== 30) throw new Error("catalogue lane must ask for 30 rows, asked " + askedLimit)
            if (hits.length !== 3) throw new Error("expected 3 run cards, got " + hits.length)

            // Run disambiguation (screenshot bug 2026-07-16, must not regress): same-title
            // runs carry their year on the CARD, stay distinct, keep data.title PURE.
            var seen = {}
            for (var i = 0; i < hits.length; ++i) {
                var h = hits[i]
                if (!h.data || h.data.gcd !== true) throw new Error("card missing gcd routing data")
                if (h.data.title !== "Justice League") throw new Error("data.title must stay pure, got " + h.data.title)
                if (seen[h.title]) throw new Error("run cards not distinct: " + h.title + " repeats")
                seen[h.title] = true
            }
            if (!seen["Justice League (2016)"] || !seen["Justice League (2018)"])
                throw new Error("dated runs missing year suffix: " + Object.keys(seen).join(" | "))
            if (!seen["Justice League"]) throw new Error("yearless run must render undecorated")
            if (hits[1].data.gcdId !== 6) throw new Error("engine order must be preserved (C++ ranks)")

            // Degrades: no engine / engine not ready / short query -> [] (manga lane lives on).
            if (WorldSearch.searchCatalogDb("justice league", null).length !== 0)
                throw new Error("null engine must yield empty")
            if (WorldSearch.searchCatalogDb("justice league", { ready: function() { return false } }).length !== 0)
                throw new Error("not-ready engine must yield empty")
            if (WorldSearch.searchCatalogDb("j", fakeEngine).length !== 0)
                throw new Error("sub-2-char query must yield empty")

            // Legacy lanes are GONE — written over, not parked (spec 2026-07-18).
            if (typeof WorldSearch.searchCatalog === "function") throw new Error("curated searchCatalog must be deleted")
            if (typeof WorldSearch.searchWestern === "function") throw new Error("live searchWestern must be deleted")
            if (typeof WorldSearch.searchLocg === "function") throw new Error("parked searchLocg must be deleted")
            if (typeof WorldSearch.mergeTankobanResults === "function") throw new Error("mergeTankobanResults must be deleted")

            // Merge: an exact-titled catalogue hit must win Top Match over manga rows.
            var mangaLane = [
                { title: "Some Manga", group: "Manga", data: { title: "Some Manga" } },
                { title: "Justice Beach", group: "Manga", data: { title: "Justice Beach" } }
            ]
            var merged = WorldSearch.mergeSearchLanes("justice league", mangaLane, hits)
            if (merged.length !== 5) throw new Error("merge must keep every row, got " + merged.length)
            if (!merged[0].data.gcd) throw new Error("exact catalogue hit must win Top Match")

            console.log("WORLD_SEARCH_COMICS_OK")
            Qt.exit(0)
        } catch (error) {
            console.error("WORLD_SEARCH_COMICS_FAIL", error)
            Qt.exit(1)
        }
    }
}
