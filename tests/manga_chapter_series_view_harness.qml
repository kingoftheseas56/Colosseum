import QtQuick

Item {
    id: harness
    width: 1280; height: 900

    QtObject {
        id: downloads
        property var requested: []
        signal progress(string chapterId, int done, int total)
        signal finished(string chapterId)
        signal failed(string chapterId, string reason)
        signal removed(string chapterId)
        signal thumbReady(string chapterId, string url)
        function statusOf(id) { return { state: "none", done: 0, total: 0 } }
        function localPages(id) { return [] }
        function fetchThumb(seriesId, chapterId) {
            var rows = requested.slice(); rows.push(String(chapterId)); requested = rows
            thumbReady(String(chapterId), "")
        }
        function downloadChapter(id, sid, title, label) {}
        function cancelDownload(id) {}
        function deleteChapter(id) { return ({ success: true }) }
    }

    property var view: null
    property int extensionRequests: 0
    function fail(msg) { console.error("MANGA_CHAPTER_VIEW_FAIL: " + msg); Qt.exit(1) }
    function check(ok, msg) { if (!ok) fail(msg) }
    function chapters(n) {
        var out = []
        for (var i = 1; i <= n; ++i)
            out.push({ id: "ch-" + i, seriesId: "wc-series", number: i,
                       label: "Chapter " + i, name: "Chapter " + i, rawOrder: i })
        return out
    }

    function verifyFirstPage() {
        check(view.pageCount === 3, "25 chapters must expose 3 pages")
        view.sourceEnabled = false
        check(view.extensionGateVisible === true, "disabled source exposes the Extensions gate")
        view.requestExtensions()
        check(harness.extensionRequests === 1, "Extensions gate emits exactly one navigation request")
        view.sourceEnabled = true
        check(view.extensionGateVisible === false, "enabled source hides the Extensions gate")
        check(view.currentPageIndex === 0, "initial page index")
        check(view.pageSelectorLabel === "Page 1", "single selector starts at Page 1")
        check(view.pageSelectorControlCount === 1, "there must be exactly one page selector control")
        check(view.activeChapterCount === 10, "Page 1 must contain 10 chapters")
        check(view.activeThumbnailDelegateCount <= 10, "active thumbnail delegate budget")
        check(downloads.requested.length === 10, "inactive pages must not request thumbnails")
        view.selectPage(1)
        Qt.callLater(verifySecondPage)
    }

    function verifySecondPage() {
        check(view.pageSelectorLabel === "Page 2", "selector label follows selected page")
        check(view.activeChapterCount === 10, "Page 2 must contain 10 chapters")
        check(view.activeThumbnailDelegateCount <= 10, "Page 2 delegate budget")
        check(downloads.requested.length === 20, "Page 2 alone adds ten thumbnail requests")
        view.selectPage(2)
        Qt.callLater(verifyThirdPage)
    }
    function verifyThirdPage() {
        check(view.pageSelectorLabel === "Page 3", "selector can move beyond Page 2")
        check(view.activeChapterCount === 5, "Page 3 contains the remainder")
        check(view.activeThumbnailDelegateCount <= 10, "remainder respects delegate budget")
        check(downloads.requested.length === 25, "only active-page thumbnails are requested")
        console.log("MANGA_CHAPTER_VIEW_OK")
        Qt.exit(0)
    }

    Component.onCompleted: {
        var comp = Qt.createComponent("../qml/MangaChapterSeriesView.qml")
        if (comp.status === Component.Error) fail("component: " + comp.errorString())
        view = comp.createObject(harness, {
            width: 1280, height: 900,
            seriesId: "mal:13", sourceSeriesId: "wc-series",
            seriesTitle: "One Piece", author: "Eiichiro Oda",
            status: "Publishing", year: 1997, score: 9.22,
            synopsis: "Pirates, promises, and the Grand Line.",
            chapters: chapters(25), downloader: downloads
        })
        if (!view) fail("could not create chapter series view")
        view.openExtensionsRequested.connect(function() { harness.extensionRequests += 1 })
        Qt.callLater(verifyFirstPage)
    }
}
