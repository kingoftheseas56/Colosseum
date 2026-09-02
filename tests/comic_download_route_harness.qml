// Linux 1.1.6 T-03: completed comic rows must reopen the owning reader lane.
import QtQuick
import QtQuick.Window
import "../qml/ComicDownloadRoute.js" as ComicDownloadRoute

Window {
    visible: false
    property var failures: []
    function ck(v, msg) { if (!v) failures.push(msg) }
    Component.onCompleted: {
        ck(ComicDownloadRoute.destination({seriesId:"gc:chew", packRole:""}) === "getcomics",
           "slug gc: ids must keep the GetComics shelf route")
        ck(ComicDownloadRoute.destination({seriesId:"gc:15422", packRole:""}) === "locg",
           "numeric gc: ids must reopen the DB/LOCG comic lane")
        ck(ComicDownloadRoute.destination({seriesId:"gcd:119237", packRole:""}) === "gcd",
           "gcd: ids must keep the baked catalogue route")
        ck(ComicDownloadRoute.destination({seriesId:"pack:chew", packRole:"main"}) === "pack",
           "demuxed pack rows must keep the pack route")
        if (failures.length) {
            for (var i = 0; i < failures.length; ++i) console.log("COMIC_DOWNLOAD_ROUTE_FAIL: " + failures[i])
            Qt.exit(1)
        }
        console.log("COMIC_DOWNLOAD_ROUTE_OK")
        Qt.exit(0)
    }
}
