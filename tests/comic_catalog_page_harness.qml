import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: false

    Loader {
        id: pageLoader
        anchors.fill: parent
        source: "../qml/ComicCatalogPage.qml"
        onLoaded: {
            item.rows = [
                { locgId: "locg:1", title: "Alpha", caption: "Alpha", cover: "", publisher: "Ink House" },
                { locgId: "locg:2", title: "Beta", caption: "Beta", cover: "", publisher: "Paper House" }
            ]
            Qt.callLater(function() {
                if (item.catalogRows.length !== 2
                        || item.catalogRows[0].displayRank !== 1
                        || item.catalogRows[1].displayRank !== 2) {
                    console.log("COMIC_CATALOG_PAGE_FAIL bad projection")
                    Qt.exit(2)
                    return
                }
                item.applyView("BETA", false)
                if (item.visibleRows.length !== 1 || item.visibleRows[0].title !== "Beta") {
                    console.log("COMIC_CATALOG_PAGE_FAIL bad search")
                    Qt.exit(2)
                    return
                }
                item.clearView()
                console.log("COMIC_CATALOG_PAGE_OK")
                Qt.exit(0)
            })
        }
    }

    Timer {
        interval: 5000
        running: true
        onTriggered: {
            console.log("COMIC_CATALOG_PAGE_FAIL timeout")
            Qt.exit(3)
        }
    }
}
