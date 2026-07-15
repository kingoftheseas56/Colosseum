import QtQuick
import "../qml/WorldSearch.js" as WorldSearch
import "../qml/ComicsDb.js" as ComicsDb

Item {
    Component.onCompleted: {
        try {
            var rows = []
            for (var i = 1; i <= 688; ++i) {
                rows.push({
                    rank: i,
                    title: i === 2 ? "The Walking Dead"
                         : i === 3 ? "Justice League"
                         : i === 4 ? "Justice League Dark"
                         : i === 5 ? "Nickelodeon Avatar: The Last Airbender - The Promise"
                         : "Catalog Comic " + i,
                    locg_id: String(100000 + i),
                    publisher: i === 2 ? "Image Comics" : "Test Press",
                    cover: "cover-" + i,
                    editions: []
                })
            }
            if (!ComicsDb.setData({ series: rows })) throw new Error("catalog ingest failed")

            var all = ComicsDb.rankedSeries()
            if (all.length !== 688) throw new Error("expected 688 loaded rows")

            var hits = WorldSearch.searchCatalog("walking dead")
            if (hits.length !== 1) throw new Error("expected one local title hit")
            if (!hits[0].data.locg || hits[0].data.id !== "locg:100002")
                throw new Error("local hit does not route through LOCG identity")

            var merged = WorldSearch.mergeTankobanResults("walking dead", [], hits, [{
                title: "The Walking Dead",
                group: "Comics · GetComics",
                data: { western: true }
            }])
            var count = 0
            for (var j = 0; j < merged.length; ++j)
                if (String(merged[j].title).toLowerCase() === "the walking dead") count += 1
            if (count !== 1) throw new Error("local/GetComics duplicate survived")
            if (!merged[0].data.locg) throw new Error("local catalog identity did not win dedupe")

            // Screenshot bug 2026-07-15: a query LONGER than the catalog title
            // ("justice league unlimited" vs our "Justice League") zeroed the whole
            // catalog lane, leaving GetComics alone in the results. Token-subset
            // queries must still surface our own DB rows.
            var jlu = WorldSearch.searchCatalog("justice league unlimited")
            if (jlu.length < 2) throw new Error("longer-than-title query dropped catalog rows")
            if (String(jlu[0].title) !== "Justice League")
                throw new Error("closest catalog run should lead, got " + jlu[0].title)

            // ...and the reverse: query tokens inside a longer catalog title.
            var promise = WorldSearch.searchCatalog("avatar promise")
            if (promise.length !== 1 || promise[0].title.indexOf("Promise") < 0)
                throw new Error("token-subset query missed the longer catalog title")

            // Junk guard: stopword-only overlap must not match.
            var junk = WorldSearch.searchCatalog("the of and")
            if (junk.length !== 0) throw new Error("stopword-only query matched catalog rows")

            // Grouping: with the catalog lane alive, our DB rows must sit ABOVE the
            // GetComics rows in the merged order (GetComics = bottom shelf).
            var merged2 = WorldSearch.mergeTankobanResults("justice league unlimited", [], jlu, [{
                title: "Justice League Unlimited",
                group: "Comics · GetComics",
                data: { western: true }
            }])
            var firstCatalog = -1, firstWestern = -1
            for (var k = 0; k < merged2.length; ++k) {
                if (merged2[k].data && merged2[k].data.locg && firstCatalog < 0) firstCatalog = k
                if (merged2[k].data && merged2[k].data.western && firstWestern < 0) firstWestern = k
            }
            if (firstCatalog < 0) throw new Error("catalog rows missing from merge")
            // The exact-titled GetComics hit may win Top Match (slot 0) — honest, we
            // don't carry JLU — but the catalog block must not sit wholly below the
            // western lane.
            if (firstWestern >= 0 && firstCatalog > 1)
                throw new Error("catalog rows sank below the GetComics shelf")

            console.log("WORLD_SEARCH_COMICS_OK", all.length)
            Qt.exit(0)
        } catch (error) {
            console.error("WORLD_SEARCH_COMICS_FAIL", error)
            Qt.exit(1)
        }
    }
}
