import QtQuick
import "../qml/ComicsDb.js" as ComicsDb
import "../qml/comics_db.gen.js" as ComicsDbData

QtObject {
    Component.onCompleted: {
        try {
            if (!ComicsDb.setData(ComicsDbData.data))
                throw new Error("generated catalog ingest failed")
            var rows = ComicsDb.rankedSeries()
            if (rows.length !== 688)
                throw new Error("expected 688 ranked rows, got " + rows.length)
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
