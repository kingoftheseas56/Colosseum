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
                    title: i === 2 ? "The Walking Dead" : "Catalog Comic " + i,
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

            console.log("WORLD_SEARCH_COMICS_OK", all.length)
            Qt.exit(0)
        } catch (error) {
            console.error("WORLD_SEARCH_COMICS_FAIL", error)
            Qt.exit(1)
        }
    }
}
