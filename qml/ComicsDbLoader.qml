// ComicsDbLoader — on-demand catalog ingest for shell-side routing.
//
// Main.qml deliberately never imports comics_db.gen.js (multi-MB parse at
// startup). But DB-first series routing (Continue detail, search rows) can
// fire BEFORE the lazy Tankoban world has ingested the catalog. This tiny
// component carries the import instead: Main activates it once, on the first
// route that needs the catalog, and pays the parse exactly when it's needed.
import QtQuick
import "comics_db.gen.js" as ComicsDbData
import "ComicsDb.js" as ComicsDb

Item {
    Component.onCompleted: {
        if (!ComicsDb.ready() && !ComicsDb.setData(ComicsDbData.data))
            console.warn("ComicsDbLoader: catalog ingest failed")
    }
}
