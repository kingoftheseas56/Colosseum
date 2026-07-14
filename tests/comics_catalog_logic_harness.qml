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
            if (rows.length !== 688)
                throw new Error("expected 688 ranked rows, got " + rows.length)
            var prepared = CatalogModel.prepare(rows, ComicsDb.hasDownloadableEdition)
            if (prepared.length !== 688 || prepared[0].displayRank !== 1
                    || prepared[prepared.length - 1].displayRank !== 688)
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
            console.log("COMICS_CATALOG_OK " + rows.length)
            Qt.exit(0)
        } catch (error) {
            console.log("COMICS_CATALOG_FAIL " + error.message)
            Qt.exit(2)
        }
    }
}
