// biblio_consumption_intent_harness.qml — Arc 19 deterministic Biblio consume-vs-acquire gate.
// Copy beside Colosseum/tests after adopting the candidate BiblioBook.qml.
import QtQuick
import QtQuick.Window

Window {
    id: harness
    width: 1280
    height: 820
    visible: true

    component FakeBooks: QtObject {
        property var states: ({})
        property var files: ({})
        property int downloadCalls: 0
        property int deleteCalls: 0
        signal resolving(string md5)
        signal progress(string md5, real received, real total)
        signal finished(string md5, string path)
        signal failed(string md5, string reason)
        signal removed(string md5)

        function copyMap(source) {
            var out = ({})
            for (var k in source) out[k] = source[k]
            return out
        }
        function setState(id, state, path, received, total) {
            var next = copyMap(states)
            next[String(id)] = { state: String(state), received: Number(received || 0), total: Number(total || 0) }
            states = next
            var nextFiles = copyMap(files)
            nextFiles[String(id)] = path || ""
            files = nextFiles
        }
        function statusOf(id) {
            return states[String(id)] !== undefined
                ? states[String(id)] : { state: "none", received: 0, total: 0 }
        }
        function localBook(id) { return files[String(id)] || "" }
        function isDownloaded(id) { return localBook(id).length > 0 }
        function downloadBook(id, name, title, bytes, author) {
            downloadCalls += 1
            setState(String(id), "resolving", "", 0, 0)
            resolving(String(id))
        }
        function deleteBook(id) {
            deleteCalls += 1
            setState(String(id), "none", "", 0, 0)
            removed(String(id))
            return ({ success: true })
        }
        function finish(id, path) {
            setState(String(id), "done", String(path), 100, 100)
            finished(String(id), String(path))
        }
        function fail(id, reason) {
            setState(String(id), "none", "", 0, 0)
            failed(String(id), String(reason || "network failure"))
        }
    }

    component FakeBookTorrents: QtObject {
        property var states: ({})
        property var files: ({})
        property int downloadCalls: 0
        property int deleteCalls: 0
        signal resultsReady(var rows)
        signal searchFinished()
        signal resolving(string infoHash)
        signal progress(string infoHash, real received, real total)
        signal finished(string infoHash, string path)
        signal failed(string infoHash, string reason)
        signal removed(string infoHash)

        function copyMap(source) {
            var out = ({})
            for (var k in source) out[k] = source[k]
            return out
        }
        function setState(id, state, path, received, total) {
            var key = String(id).toLowerCase()
            var next = copyMap(states)
            next[key] = { state: String(state), received: Number(received || 0), total: Number(total || 0) }
            states = next
            var nextFiles = copyMap(files)
            nextFiles[key] = path || ""
            files = nextFiles
        }
        function statusOf(id) {
            var key = String(id).toLowerCase()
            return states[key] !== undefined ? states[key] : { state: "none", received: 0, total: 0 }
        }
        function localFile(id) { return files[String(id).toLowerCase()] || "" }
        function isDownloaded(id) { return localFile(id).length > 0 }
        function search(title, author) {}
        function download(id, title, author) {
            var key = String(id).toLowerCase()
            downloadCalls += 1
            setState(key, "resolving", "", 0, 0)
            resolving(key)
        }
        function deleteDownload(id) {
            var key = String(id).toLowerCase()
            deleteCalls += 1
            setState(key, "none", "", 0, 0)
            removed(key)
        }
        function finish(id, path) {
            var key = String(id).toLowerCase()
            setState(key, "done", String(path), 100, 100)
            finished(key, String(path))
        }
        function fail(id, reason) {
            var key = String(id).toLowerCase()
            setState(key, "none", "", 0, 0)
            failed(key, String(reason || "torrent failure"))
        }
    }

    component FakeCollection: QtObject {
        property int addCalls: 0
        function add(world, entry) { addCalls += 1 }
    }

    component FakeProgress: QtObject {
        property string expectedId: ""
        property real fraction: 0.48
        function get(kind, id) {
            if (kind === "book" && String(id) === expectedId)
                return { progress: fraction }
            return ({})
        }
    }

    FakeBooks { id: books }
    FakeBookTorrents { id: torrents }
    FakeCollection { id: collection }
    FakeProgress { id: progress }

    property var page: null
    property int openedCount: 0
    property string openedPath: ""
    property var openedBook: ({})

    property var ed1: ({ md5: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", format: "epub", best: true,
                         size: "3 MB", source: "libgen", language: "English" })
    property var ed2: ({ md5: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", format: "pdf", best: false,
                         size: "9 MB", source: "libgen", language: "English" })
    property var externalEd: ({ md5: "", format: "epub", best: true,
                                url: "https://example.invalid/book", source: "external" })
    property string hash1: "1111111111111111111111111111111111111111"
    property string hash2: "2222222222222222222222222222222222222222"
    property var tor1: ({ infoHash: hash1, title: "Arc 19 Book EPUB", format: "EPUB",
                          seeders: 42, size: "4 MB", pack: false })
    property var tor2: ({ infoHash: hash2, title: "Arc 19 Book PDF", format: "PDF",
                          seeders: 18, size: "9 MB", pack: false })

    function ck(value, message) {
        if (!value) throw new Error(message)
    }
    function clearOpen() {
        openedCount = 0
        openedPath = ""
        openedBook = ({})
    }
    function resetPageState() {
        page._invalidateReadIntent()
        page.readError = ""
        page.localPath = ""
        page.edLoading = false
        page.torLoading = false
        page.editions = []
        page.torrents = []
        page.readChoiceOpen = false
        page.readChoiceRows = []
        clearOpen()
    }
    function makePage() {
        var comp = Qt.createComponent("../qml/BiblioBook.qml")
        if (comp.status === Component.Error) throw new Error(comp.errorString())
        var p = comp.createObject(harness, {
            width: harness.width,
            height: harness.height,
            booksRef: books,
            bookTorrentsRef: torrents,
            collectionRef: collection,
            progressRef: progress
        })
        if (!p) throw new Error("BiblioBook candidate did not instantiate")
        p.readRequested.connect(function(path, book) {
            openedCount += 1
            openedPath = String(path || "")
            openedBook = book || ({})
        })
        return p
    }

    function runChecks() {
        try {
            page = makePage()

            // Ready local Read uses the same reader seam immediately and starts no acquisition.
            resetPageState()
            page.editions = [ed1]
            books.setState(ed1.md5, "done", "C:/arc19/ready.epub", 100, 100)
            page.refreshLocal()
            progress.expectedId = page.localPath
            ck(page.primaryReadLabel() === "Continue", "local progress must surface Continue")
            ck(page.primaryReadStatus().indexOf("48% read") === 0,
               "reading progress must stay separate from acquisition progress")
            var bookCalls = books.downloadCalls
            page.readBook()
            ck(openedCount === 1 && openedPath === "C:/arc19/ready.epub",
               "ready Read must open the exact local path immediately")
            ck(books.downloadCalls === bookCalls, "ready Read must not start a download")
            ck(!page.pendingReadActive, "ready Read must leave no pending foreground intent")
            // Fresh Read through Books: one assertion starts exactly one tracked acquisition.
            resetPageState()
            books.setState(ed1.md5, "none", "", 0, 0)
            page.editions = [ed1]
            bookCalls = books.downloadCalls
            page.readBook()
            ck(books.downloadCalls === bookCalls + 1, "fresh Books Read must start exactly one download")
            ck(page.pendingReadTransport === "books", "fresh Books Read must bind Books transport")
            ck(page.pendingReadAcquisitionId === ed1.md5, "fresh Books Read must bind exact md5")
            ck(openedCount === 0, "fresh Read must not open before completion")

            // Wrong md5 and wrong transport completions cannot satisfy the exact pending Read.
            books.finish(ed2.md5, "C:/arc19/wrong.pdf")
            ck(openedCount === 0 && page.pendingReadAcquisitionId === ed1.md5,
               "wrong Books completion must be ignored")
            torrents.finish(hash1, "C:/arc19/wrong-torrent.epub")
            ck(openedCount === 0 && page.pendingReadTransport === "books",
               "torrent completion must not satisfy a Books Read")

            // Exact completion opens once; duplicate completion is inert.
            books.finish(ed1.md5, "C:/arc19/fresh.epub")
            ck(openedCount === 1 && openedPath === "C:/arc19/fresh.epub",
               "exact Books completion must open Reader2 through readRequested")
            ck(!page.pendingReadActive, "successful completion must consume foreground intent")
            books.finished(ed1.md5, "C:/arc19/fresh.epub")
            ck(openedCount === 1, "duplicate Books completion must not open twice")

            // Explicit edition Download is acquire-only.
            resetPageState()
            books.setState(ed2.md5, "none", "", 0, 0)
            page.editions = [ed2]
            bookCalls = books.downloadCalls
            page.downloadEdition(ed2)
            ck(books.downloadCalls === bookCalls + 1, "explicit edition Download must start acquisition")
            ck(!page.pendingReadActive, "explicit edition Download must not create Read intent")
            books.finish(ed2.md5, "C:/arc19/download-only.pdf")
            ck(openedCount === 0, "explicit edition Download completion must stay on detail page")
            // Read adopts an already-running Books job without restart.
            resetPageState()
            books.setState(ed1.md5, "downloading", "", 44, 100)
            page.editions = [ed1]
            bookCalls = books.downloadCalls
            page.readBook()
            ck(books.downloadCalls === bookCalls, "Read must adopt an existing Books job")
            ck(page.pendingReadTransport === "books" && page.pendingReadAcquisitionId === ed1.md5,
               "adopted Books Read must retain exact transport and md5")
            ck(page.primaryReadStatus().indexOf("44%") >= 0,
               "foreground acquisition progress must be visible without becoming reading progress")
            books.finish(ed1.md5, "C:/arc19/adopted.epub")
            ck(openedCount === 1 && openedPath === "C:/arc19/adopted.epub",
               "adopted Books job must open when its exact file becomes ready")

            // Read adopts an already-running torrent job without starting a LibGen replacement.
            resetPageState()
            page.editions = [ed1]
            page.torrents = [tor1]
            books.setState(ed1.md5, "none", "", 0, 0)
            torrents.setState(hash1, "downloading", "", 62, 100)
            var torrentCalls = torrents.downloadCalls
            bookCalls = books.downloadCalls
            page.readBook()
            ck(torrents.downloadCalls === torrentCalls && books.downloadCalls === bookCalls,
               "in-flight torrent adoption must not start another transport")
            ck(page.pendingReadTransport === "torrent" && page.pendingReadAcquisitionId === hash1,
               "torrent adoption must bind the exact infoHash")
            torrents.finish(hash1, "C:/arc19/adopted-torrent.epub")
            ck(openedCount === 1 && openedPath === "C:/arc19/adopted-torrent.epub",
               "adopted torrent must open on exact completion")

            // Explicit torrent Download remains acquire-only.
            resetPageState()
            page.torrents = [tor2]
            torrents.setState(hash2, "none", "", 0, 0)
            torrentCalls = torrents.downloadCalls
            page.downloadTorrent(tor2)
            ck(torrents.downloadCalls === torrentCalls + 1, "explicit torrent Download must start one job")
            ck(!page.pendingReadActive, "explicit torrent Download must not create Read intent")
            torrents.finish(hash2, "C:/arc19/download-only-torrent.pdf")
            ck(openedCount === 0, "explicit torrent completion must never auto-open Reader2")
            // Leaving the detail context cancels auto-open but not the durable acquisition.
            resetPageState()
            page.editions = [ed1]
            books.setState(ed1.md5, "none", "", 0, 0)
            page.readBook()
            ck(page.pendingReadAcquisitionId === ed1.md5, "cancellation case must start with pending Read")
            page._invalidateReadIntent()
            ck(books.statusOf(ed1.md5).state === "resolving",
               "abandoning Read must not cancel the underlying Books job")
            books.finish(ed1.md5, "C:/arc19/background-finished.epub")
            ck(openedCount === 0, "completion after navigation cancellation must not steal focus")

            // Newer Read generation wins; stale completion from the superseded edition is ignored.
            resetPageState()
            books.setState(ed1.md5, "none", "", 0, 0)
            books.setState(ed2.md5, "none", "", 0, 0)
            page.editions = [ed1]
            page.readBook()
            ck(page.pendingReadAcquisitionId === ed1.md5, "first generation must target edition one")
            page.editions = [ed2]
            page.readBook()
            ck(page.pendingReadAcquisitionId === ed2.md5, "new Read must supersede with edition two")
            books.finish(ed1.md5, "C:/arc19/stale.epub")
            ck(openedCount === 0, "stale generation completion must not open Reader2")
            books.finish(ed2.md5, "C:/arc19/current.pdf")
            ck(openedCount === 1 && openedPath === "C:/arc19/current.pdf",
               "current generation completion must open exactly once")
            // Failure clears foreground auto-open and exposes an honest retry state.
            resetPageState()
            page.editions = [ed1]
            books.setState(ed1.md5, "none", "", 0, 0)
            page.readBook()
            books.fail(ed1.md5, "temporary network failure")
            ck(!page.pendingReadActive && openedCount === 0,
               "failed foreground Read must clear intent without opening")
            ck(page.readError.indexOf("temporary network failure") >= 0,
               "failed foreground Read must retain the actual failure reason")

            // External preferred edition forces an intent-aware tracked source choice.
            resetPageState()
            page.editions = [externalEd]
            page.torrents = [tor1]
            torrents.setState(hash1, "none", "", 0, 0)
            page.readBook()
            ck(page.pendingReadTransport === "choice" && page.readChoiceOpen,
               "untracked preferred edition must ask for a tracked Read source")
            ck(page.readChoiceRows.length === 1 && page.readChoiceRows[0].id === hash1,
               "source choice must expose the ranked tracked torrent")
            torrentCalls = torrents.downloadCalls
            page.chooseReadSource(page.readChoiceRows[0])
            ck(torrents.downloadCalls === torrentCalls + 1,
               "choosing a tracked source from Read must start that exact acquisition")
            ck(page.pendingReadTransport === "torrent" && page.pendingReadAcquisitionId === hash1,
               "choice must retain the original Read intent through exact infoHash")
            torrents.finish(hash1, "C:/arc19/chosen.epub")
            ck(openedCount === 1 && openedPath === "C:/arc19/chosen.epub",
               "chosen tracked source must finish in Reader2")
            // Cancelling the source chooser cancels auto-open and starts no acquisition.
            resetPageState()
            page.editions = [externalEd]
            page.torrents = [tor2]
            torrents.setState(hash2, "none", "", 0, 0)
            page.readBook()
            torrentCalls = torrents.downloadCalls
            page.cancelReadChoice()
            ck(!page.pendingReadActive && !page.readChoiceOpen,
               "cancelling source choice must clear foreground Read")
            ck(torrents.downloadCalls === torrentCalls,
               "cancelling source choice must not start a download")

            // Do not start LibGen while the torrent inventory is still loading.
            resetPageState()
            page.editions = [ed1]
            books.setState(ed1.md5, "none", "", 0, 0)
            page.torLoading = true
            bookCalls = books.downloadCalls
            page.readBook()
            ck(page.pendingReadTransport === "lookup", "Read must wait while another source inventory is loading")
            ck(books.downloadCalls === bookCalls, "waiting for source inventory must not start LibGen early")
            page.torrents = [tor1]
            torrents.setState(hash1, "downloading", "", 37, 100)
            page.torLoading = false
            page._resolvePendingLookup(page.pendingReadGeneration)
            ck(page.pendingReadTransport === "torrent" && page.pendingReadAcquisitionId === hash1,
               "late-discovered active torrent must be adopted before starting a new LibGen copy")
            ck(books.downloadCalls === bookCalls, "active torrent adoption must still avoid a new LibGen job")
            torrents.finish(hash1, "C:/arc19/late-adopted.epub")
            ck(openedCount === 1 && openedPath === "C:/arc19/late-adopted.epub",
               "late-discovered active torrent must finish in Reader2")
            // No tracked source is honest: no download and no dangling foreground intent.
            resetPageState()
            bookCalls = books.downloadCalls
            torrentCalls = torrents.downloadCalls
            page.readBook()
            ck(!page.pendingReadActive && openedCount === 0,
               "no-source Read must not leave a fake pending intent")
            ck(books.downloadCalls === bookCalls && torrents.downloadCalls === torrentCalls,
               "no-source Read must not invent an acquisition")
            ck(page.readError.indexOf("No tracked readable source") >= 0,
               "no-source Read must expose an honest recovery state")

            // Book-context identity is part of the completion guard, not just the acquisition id.
            resetPageState()
            page.editions = [ed1]
            books.setState(ed1.md5, "none", "", 0, 0)
            page.readBook()
            page.pendingReadBookIdentity = "superseded-book"
            books.finish(ed1.md5, "C:/arc19/wrong-book.epub")
            ck(openedCount === 0,
               "exact md5 completion with stale book identity must not hijack the reader")

            console.log("BIBLIO_CONSUMPTION_INTENT_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("BIBLIO_CONSUMPTION_INTENT_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    Timer {
        interval: 120
        running: true
        repeat: false
        onTriggered: Qt.callLater(harness.runChecks)
    }
    Timer {
        interval: 10000
        running: true
        repeat: false
        onTriggered: {
            console.log("BIBLIO_CONSUMPTION_INTENT_FAIL: timeout")
            Qt.exit(1)
        }
    }
}
