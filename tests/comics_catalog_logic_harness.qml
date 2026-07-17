import QtQuick
import "../qml/ComicsDb.js" as ComicsDb
import "../qml/ComicCatalogModel.js" as CatalogModel
import "../qml/comics_db.gen.js" as ComicsDbData

QtObject {
    Component.onCompleted: {
        try {
            if (!ComicsDb.setData(ComicsDbData.data))
                throw new Error("generated catalog ingest failed")
            var rows = ComicsDb.rankedSeries()
            // 806 GCD-spine rows + the VerseDB-recovered tail/top-sellers, which
            // grows with each harvest — assert the floor and the shape, not a
            // frozen total (stale-contract lesson, 2026-07-15).
            if (rows.length < 1100)
                throw new Error("expected >=1100 ranked rows, got " + rows.length)
            var prepared = CatalogModel.prepare(rows, ComicsDb.hasDownloadableEdition)
            if (prepared.length !== rows.length || prepared[0].displayRank !== 1
                    || prepared[prepared.length - 1].displayRank !== rows.length)
                throw new Error("display ranks are not sequential")
            var seenRoutes = ({})
            for (var i = 0; i < rows.length; i++) {
                if (!rows[i].locgId || rows[i].locgId.length === 0)
                    throw new Error("ranked row " + i + " has no route id")
                if (seenRoutes[rows[i].locgId])
                    throw new Error("duplicate ranked route " + rows[i].locgId)
                seenRoutes[rows[i].locgId] = true
                var routed = ComicsDb.series(rows[i].locgId)
                if (!routed || !routed.editions || routed.editions.length === 0)
                    throw new Error("ranked route " + rows[i].locgId + " has no editions")
                if (prepared[i].locgId !== rows[i].locgId)
                    throw new Error("prepare changed canonical order at " + i)
            }
            var titleHit = CatalogModel.filter(prepared, rows[0].title.toUpperCase(), false)
            if (!titleHit.length || titleHit[0].locgId !== rows[0].locgId)
                throw new Error("title search is not case-insensitive")
            var publisherRow = null
            for (var p = 0; p < prepared.length; p++) {
                if (prepared[p].publisher && prepared[p].publisher.length > 2) {
                    publisherRow = prepared[p]
                    break
                }
            }
            if (!publisherRow)
                throw new Error("catalog has no publisher fixture")
            var publisherHit = CatalogModel.filter(prepared, publisherRow.publisher.toUpperCase(), false)
            if (!publisherHit.some(function(row) { return row.locgId === publisherRow.locgId }))
                throw new Error("publisher search is not case-insensitive")
            var downloadable = CatalogModel.filter(prepared, "", true)
            if (!downloadable.length || downloadable.length >= prepared.length)
                throw new Error("downloadable filter did not create an honest subset")
            for (var d = 0; d < downloadable.length; d++) {
                if (!downloadable[d].downloadable
                        || !ComicsDb.hasDownloadableEdition(downloadable[d].locgId))
                    throw new Error("downloadable filter admitted an unavailable series")
                if (d > 0 && downloadable[d].displayRank <= downloadable[d - 1].displayRank)
                    throw new Error("downloadable filter changed canonical order")
            }
            if (ComicsDb.downloadPost({ available: false, getcomics_post: "https://wrong" }) !== null)
                throw new Error("unavailable edition exposed a download post")
            if (ComicsDb.downloadPost({ available: true, getcomics_post: "https://right" }) !== "https://right")
                throw new Error("available edition lost its download post")
            // "Also on GetComics" rail accessor (parser+attachment arc): a series
            // carrying a `sources` array exposes it via ComicsDb.sources(); a series
            // without the field, and an unknown id, both yield []. (setData a fixture
            // LAST — it replaces the loaded catalog, and nothing follows but exit.)
            if (!ComicsDb.setData({ series: [
                    { locg_id: "srctest", title: "Src Test", editions: [],
                      sources: [ { id: 9, title: "Src Omnibus", link: "https://g/9/",
                                   kind: "collection", fan_made: false } ] },
                    { locg_id: "nosrc", title: "No Src", editions: [] } ] }))
                throw new Error("sources fixture ingest failed")
            var srcs = ComicsDb.sources("locg:srctest")
            if (!srcs || srcs.length !== 1 || srcs[0].kind !== "collection" || srcs[0].id !== 9)
                throw new Error("sources accessor broken: " + JSON.stringify(srcs))
            if (ComicsDb.sources("locg:nosrc").length !== 0)
                throw new Error("series without a rail must yield empty sources")
            if (ComicsDb.sources("locg:does-not-exist").length !== 0)
                throw new Error("unknown series must yield empty sources")
            console.log("COMICS_CATALOG_OK " + rows.length)
            Qt.exit(0)
        } catch (error) {
            console.log("COMICS_CATALOG_FAIL " + error.message)
            Qt.exit(2)
        }
    }
}
