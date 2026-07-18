// comics_catalog_logic_harness — behavioral checks for the ComicsDb.js + ComicCatalogModel.js
// logic lane, against an inline FIXTURE via the setData() test path (P4 Task 4 rewrite,
// 2026-07-18: the shipped catalog moved from the baked gen.js blob into comics_catalog.db and
// is read through the ComicsCatalog engine; the gen.js import here retired with it). The old
// >=1100-row data-scale floor moved engine-side into test_comics_catalog_db.ps1 — this file
// asserts LOGIC: ranked-row shape/route ids/dedupe, prepare/displayRank, filter case-
// insensitivity (title + publisher), the honest downloadable subset, series() routing,
// downloadPost() truth, and the sources() accessor including its empty cases.
import QtQuick
import "../qml/ComicsDb.js" as ComicsDb
import "../qml/ComicCatalogModel.js" as CatalogModel

QtObject {
    Component.onCompleted: {
        try {
            // The fixture: five ranked series exercising every filter/availability branch.
            //  - alpha1: downloadable (available + post)
            //  - beta2:  NOT downloadable (available:false)
            //  - gamma3: downloadable via its SECOND edition (mixed availability)
            //  - delta4: NOT downloadable (available but no post)
            //  - srctest: downloadable, and carries a `sources` rail fixture
            var FIXTURE = { series: [
                { rank: 1, locg_id: "alpha1", title: "Alpha Squad", publisher: "Marvel",
                  cover: "https://c/a.jpg", genres: ["Action"], editions: [
                    { title: "Alpha Squad Vol. 1", available: true, getcomics_post: "https://g/a1/" } ] },
                { rank: 2, locg_id: "beta2", title: "The Beta Files", publisher: "DC Comics",
                  cover: "https://c/b.jpg", genres: ["Crime"], editions: [
                    { title: "Beta Files HC", available: false, getcomics_post: "" } ] },
                { rank: 3, locg_id: "gamma3", title: "Gamma World", publisher: "Image",
                  cover: "", genres: ["Sci-Fi", "Action"], editions: [
                    { title: "Gamma World Book One", available: false, getcomics_post: "https://g/g3/" },
                    { title: "Gamma World Book Two", available: true, getcomics_post: "https://g/g3b/" } ] },
                { rank: 4, locg_id: "delta4", title: "Delta Force", publisher: "Dark Horse",
                  cover: "https://c/d.jpg", genres: [], editions: [
                    { title: "Delta Force Omnibus", available: true, getcomics_post: "" } ] },
                { rank: 5, locg_id: "srctest", title: "Src Test", publisher: "Boom",
                  cover: "", genres: ["Action"], editions: [
                    { title: "Src Test TP", available: true, getcomics_post: "https://g/s5/" } ],
                  sources: [ { id: 9, title: "Src Omnibus", link: "https://g/9/",
                               kind: "collection", fan_made: false } ] } ] }
            if (!ComicsDb.setData(FIXTURE))
                throw new Error("fixture ingest failed")
            var rows = ComicsDb.rankedSeries()
            if (rows.length !== 5)
                throw new Error("expected 5 ranked fixture rows, got " + rows.length)
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
            var titleHit = CatalogModel.filter(prepared, "ALPHA SQUAD", false)
            if (titleHit.length !== 1 || titleHit[0].locgId !== "locg:alpha1")
                throw new Error("title search is not case-insensitive")
            var publisherHit = CatalogModel.filter(prepared, "DARK HORSE", false)
            if (publisherHit.length !== 1 || publisherHit[0].locgId !== "locg:delta4")
                throw new Error("publisher search is not case-insensitive")
            var downloadable = CatalogModel.filter(prepared, "", true)
            if (downloadable.length !== 3 || downloadable.length >= prepared.length)
                throw new Error("downloadable filter did not create an honest subset")
            for (var d = 0; d < downloadable.length; d++) {
                if (!downloadable[d].downloadable
                        || !ComicsDb.hasDownloadableEdition(downloadable[d].locgId))
                    throw new Error("downloadable filter admitted an unavailable series")
                if (d > 0 && downloadable[d].displayRank <= downloadable[d - 1].displayRank)
                    throw new Error("downloadable filter changed canonical order")
            }
            // gamma3 is downloadable only through its SECOND edition — the mixed-availability case.
            if (!downloadable.some(function(row) { return row.locgId === "locg:gamma3" }))
                throw new Error("mixed-availability series must count as downloadable")
            if (ComicsDb.downloadPost({ available: false, getcomics_post: "https://wrong" }) !== null)
                throw new Error("unavailable edition exposed a download post")
            if (ComicsDb.downloadPost({ available: true, getcomics_post: "https://right" }) !== "https://right")
                throw new Error("available edition lost its download post")
            // "Also on GetComics" rail accessor: a series carrying a `sources` array exposes
            // it; a series without the field, and an unknown id, both yield [].
            var srcs = ComicsDb.sources("locg:srctest")
            if (!srcs || srcs.length !== 1 || srcs[0].kind !== "collection" || srcs[0].id !== 9)
                throw new Error("sources accessor broken: " + JSON.stringify(srcs))
            if (ComicsDb.sources("locg:beta2").length !== 0)
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
