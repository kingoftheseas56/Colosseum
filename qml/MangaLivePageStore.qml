// Temporary Chapter Mode adapter. Native MangaDownloader owns transport, cache and lifetime.
import QtQuick
Item {
    id: store
    visible: false
    property var downloader: null
    property string seriesId: ""
    property string chapterId: ""
    property double token: 0
    property bool ready: false
    property string error: ""
    readonly property bool liveReading: true
    signal readyToRead(string chapterId)
    signal pageAvailable(string chapterId, int index, string fileUrl)
    signal pageUnavailable(string chapterId, int index, string reason)

    function open(id, sid) {
        stop()
        chapterId = String(id || "")
        seriesId = String(sid || "")
        error = ""
        if (downloader && chapterId.length && typeof downloader.startLive === "function")
            token = Number(downloader.startLive(chapterId, seriesId))
        if (!token) error = "Live reading is unavailable."
        return token > 0
    }
    function stop() {
        var previous = token
        token = 0
        ready = false
        if (previous && downloader && typeof downloader.stopLive === "function")
            downloader.stopLive(chapterId, previous)
    }
    function localPages(id) {
        if (String(id) !== chapterId || !token || !downloader) return []
        return downloader.livePages(chapterId) || []
    }
    function pageReady(id, index) {
        var rows = localPages(id)
        return index >= 0 && index < rows.length && rows[index].pending !== true
    }
    function setFocus(id, index) {
        if (String(id) === chapterId && token && downloader)
            downloader.setLiveFocus(chapterId, index, token)
    }
    function retryPage(id, index) {
        if (String(id) === chapterId && token && downloader && !pageReady(id, index)) {
            error = ""
            downloader.retryLivePage(chapterId, index, token)
        }
    }
    function statusOf(id) { return downloader ? downloader.liveStatus(String(id)) : ({state:"none"}) }
    function downloadedChapters() { return downloader ? downloader.downloadedChapters() : [] }
    function downloadChapter(id, sid, title, label) {
        if (downloader) downloader.downloadChapter(id, sid, title, label)
    }
    function _admit() {
        if (!ready && pageReady(chapterId, 0)) {
            ready = true
            readyToRead(chapterId)
        }
    }
    Connections {
        target: store.downloader
        ignoreUnknownSignals: true
        function onLiveManifestReady(id, count, number) {
            if (String(id) !== store.chapterId || Number(number) !== store.token || !store.token) return
            store._admit()
        }
        function onLivePageReady(id, index, fileUrl, number) {
            if (String(id) !== store.chapterId || Number(number) !== store.token || !store.token) return
            store.pageAvailable(String(id), index, String(fileUrl))
            store._admit()
        }
        function onLiveFailed(id, index, reason, number) {
            if (String(id) !== store.chapterId || Number(number) !== store.token || !store.token) return
            store.error = String(reason)
            store.pageUnavailable(String(id), index, String(reason))
        }
    }
    Component.onDestruction: stop()
}
