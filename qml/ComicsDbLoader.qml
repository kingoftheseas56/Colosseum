// ComicsDbLoader — on-demand catalog handover for shell-side routing.
//
// DB-first series routing (Continue detail, search rows) can fire BEFORE the lazy
// Tankoban world has handed the catalog engine to ComicsDb.js. This tiny component
// carries that handover instead: Main activates it once, on the first route that
// needs the catalog. (P4 seam 2026-07-18: reads the ComicsCatalog engine/curated_*
// SQLite tables — no more multi-MB gen.js parse.)
import QtQuick
import "ComicsDb.js" as ComicsDb

Item {
    Component.onCompleted: {
        var ok = ComicsDb.setEngine(typeof ComicsCatalog !== "undefined" ? ComicsCatalog : null)
        if (!ok) console.warn("ComicsDbLoader: catalogue engine unavailable — curated surfaces dormant")
    }
}
