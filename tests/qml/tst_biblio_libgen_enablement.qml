import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/BiblioApi.js" as BiblioApi
import "../../qml/AbbApi.js" as Abb

TestCase {
    id: suite
    name: "BiblioLibgenEnablement"
    property int searches: 0
    property var callbacks: []
    property var originalSearch
    property var audioCallbacks: []
    property int audioSearches: 0
    property var originalAudio
    Component { id: pageComponent; App.BiblioBook { width: 960; height: 640 } }
    QtObject {
        id: extensions
        property int revision: 0
        property var rows: []
        function installed() { return rows }
        function setRows(value) { rows = value; revision++ }
    }
    QtObject {
        id: books
        property int downloads: 0
        property int deletions: 0
        property string state: "none"
        signal resolving(string md5)
        signal failed(string md5, string reason)
        signal finished(string md5, string path)
        function statusOf(id) { return { state: state, received: 0, total: 0 } }
        function localBook(id) { return state === "done" ? "/fixture/book.epub" : "" }
        function isDownloaded(id) { return state === "done" }
        function downloadedBooks() { return state === "done" ? [{ title: "Offline fixture", author: "Author", path: "/fixture/book.epub" }] : [] }
        function deleteBook(id) { deletions++; return { success: true } }
        function downloadBook(id, name, title, size, author) { downloads++ }
    }
    QtObject {
        id: torrents
        property int searches: 0
        signal resultsReady(var rows)
        signal searchFinished()
        function search(title, author) { searches++ }
        function download(id, title, author) {}
        function statusOf(id) { return { state: "none" } }
        function downloadedBooks() { return [] }
        function downloadedFiles() { return [] }
        function isDownloaded(id) { return false }
        function localFile(id) { return "" }
    }
    SignalSpy { id: reads; signalName: "readRequested" }
    function page(owner) {
        const item = createTemporaryObject(pageComponent, suite, {
            extensionsRef: owner, booksRef: books, bookTorrentsRef: torrents,
            collectionRef: null, progressRef: null
        })
        verify(item !== null)
        reads.target = item
        reads.clear()
        item.book = { title: "Offline fixture", author: "Author" }
        return item
    }
    function edition() { return { md5: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", format: "epub", best: true } }
    function enable(value) {
        extensions.setRows([{ id: "colosseum.well.libgen", enabled: value }])
    }
    function initTestCase() {
        originalSearch = BiblioApi.searchLibgen
        originalAudio = Abb.resolveAudiobook
        BiblioApi.searchLibgen = function(title, author, done) {
            suite.searches++
            suite.callbacks = suite.callbacks.concat([done])
        }
        Abb.resolveAudiobook = function(title, author, done) {
            suite.audioSearches++
            suite.audioCallbacks = suite.audioCallbacks.concat([done])
        }
    }
    function cleanupTestCase() {
        BiblioApi.searchLibgen = originalSearch
        Abb.resolveAudiobook = originalAudio
    }
    function init() {
        extensions.setRows([])
        searches = 0; callbacks = []; audioCallbacks = []; audioSearches = 0
        books.downloads = 0; books.deletions = 0; books.state = "none"
        torrents.searches = 0
    }
    function test_missing_extension_does_not_search() {
        const item = page(null)
        compare(searches, 0)
    }
    function test_disabled_or_missing_entry_blocks_stale_acquisition() {
        const item = page(extensions)
        compare(searches, 0)
        item.editions = [edition()]
        item.downloadEdition(edition())
        compare(books.downloads, 0)
        compare(books.deletions, 0)
        verify(item.readError.indexOf("disabled") >= 0)
        const gen = item._beginReadIntent()
        item._targetReadCandidate(item._candidate("books", edition().md5, "EPUB", "", edition()), gen)
        compare(books.downloads, 0)
        compare(books.deletions, 0)
        verify(!item.pendingReadActive)
        enable(false)
        item.loadEditions()
        compare(searches, 0)
        compare(item.edLoading, false)
    }
    function test_toggle_retires_only_libgen_and_reenable_queries_once() {
        enable(true)
        const item = page(extensions)
        compare(searches, 1)
        compare(audioSearches, 1)
        compare(torrents.searches, 1)
        compare(item.edLoading, true)
        enable(false)
        compare(item.edLoading, false)
        compare(item.editions.length, 0)
        callbacks[0]([edition()])
        compare(item.editions.length, 0)
        audioCallbacks[0]({ rows: [{ slug: "fixture" }] })
        compare(item.abRows.length, 1)
        compare(item.abLoading, false)
        torrents.resultsReady([{ infoHash: "fixture-torrent", title: "Fixture", format: "EPUB" }])
        compare(item.torrents.length, 1)
        enable(true)
        compare(searches, 2)
        compare(audioSearches, 1)
        compare(torrents.searches, 1)
        extensions.setRows([{ id: "colosseum.well.libgen", enabled: true },
                            { id: "other", enabled: true }])
        compare(searches, 2)
        callbacks[1]([edition()])
        compare(item.editions.length, 1)
        compare(item.edLoading, false)
        extensions.setRows([])
        compare(item.editions.length, 0)
        compare(searches, 2)
        compare(books.deletions, 0)
    }
    function test_enabled_acquisition_and_disabled_local_read() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        item.downloadEdition(edition())
        compare(books.downloads, 1)
        books.state = "done"
        item.refreshLocal()
        enable(false)
        compare(item.localPath, "/fixture/book.epub")
        item.readBook()
        compare(reads.count, 1)
        compare(reads.signalArguments[0][0], "/fixture/book.epub")
        compare(books.downloads, 1)
        compare(books.deletions, 0)
    }
    function test_disabling_keeps_existing_foreground_job() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        books.state = "downloading"
        item.readBook()
        compare(item.pendingReadTransport, "books")
        enable(false)
        compare(item.pendingReadTransport, "books")
        books.state = "done"
        books.finished(edition().md5, "/fixture/book.epub")
        compare(reads.count, 1)
        compare(books.downloads, 0)
        compare(books.deletions, 0)
    }
    function test_disabled_stale_choice_cannot_start_download() {
        enable(true)
        const item = page(extensions)
        const gen = item._beginReadIntent()
        item._openReadChoice([item._candidate("books", edition().md5, "EPUB", "", edition())], gen)
        enable(false)
        item.chooseReadSource(item.readChoiceRows[0])
        compare(books.downloads, 0)
        compare(books.deletions, 0)
        verify(!item.pendingReadActive)
    }
    function test_disable_settles_pending_lookup_without_retiring_other_searches() {
        enable(true)
        const item = page(extensions)
        torrents.searchFinished()
        item.readBook()
        compare(item.pendingReadTransport, "lookup")
        enable(false)
        verify(!item.pendingReadActive)
        compare(item.abLoading, true)
        compare(audioSearches, 1)
        compare(torrents.searches, 1)
    }
    function test_disabled_cold_open_finds_local_copy_before_other_callbacks() {
        enable(false)
        books.state = "done"
        const item = page(extensions)
        compare(item.abLoading, true)
        compare(item.torLoading, true)
        compare(item.localPath, "/fixture/book.epub")
        item.readBook()
        compare(reads.count, 1)
        compare(reads.signalArguments[0][0], "/fixture/book.epub")
        compare(books.downloads, 0)
        compare(searches, 0)
    }
    function test_enabled_initial_book_property_queries_once() {
        enable(true)
        const item = createTemporaryObject(pageComponent, suite, {
            extensionsRef: extensions, booksRef: books, bookTorrentsRef: torrents,
            collectionRef: null, progressRef: null,
            book: { title: "Offline fixture", author: "Author" }
        })
        verify(item !== null)
        compare(searches, 1)
    }
    function test_explicit_failure_surfaces_reason_for_current_title() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        item.downloadEdition(edition())
        books.failed(edition().md5, "HTTP status 503")
        verify(item.readError.indexOf("HTTP status 503") >= 0)
    }
    function test_queued_notification_uses_canonical_status() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        books.state = "queued"
        item.readBook()
        books.resolving(edition().md5)
        compare(item.pendingReadState, "queued")
        const row = findChild(item, "biblioEdition_" + edition().md5)
        verify(row !== null)
        compare(row.dlState, "queued")
        books.state = "none"
        books.resolving(edition().md5)
        compare(row.dlState, "resolving")
        compare(item.pendingReadState, "resolving")
    }
    function test_unrelated_and_previous_title_failure_do_not_contaminate_error() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        item.downloadEdition(edition())
        item.readError = "Keep current message"
        books.failed("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "Unrelated failure")
        compare(item.readError, "Keep current message")
        item.book = { title: "New title", author: "Author" }
        item.editions = [edition()]
        item.readError = "New title message"
        books.failed(edition().md5, "Previous title failure")
        compare(item.readError, "New title message")
    }
    function test_explicit_failure_does_not_disturb_another_pending_read() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        item.downloadEdition(edition())
        const other = { md5: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", format: "epub" }
        const gen = item._beginReadIntent()
        item._targetReadCandidate(item._candidate("books", other.md5, "EPUB", "", other), gen)
        item.readError = "Foreground message"
        const state = item.pendingReadState
        books.failed(edition().md5, "Explicit job failure")
        compare(item.readError, "Foreground message")
        compare(item.pendingReadAcquisitionId, other.md5)
        compare(item.pendingReadState, state)
        verify(item.pendingReadActive)
    }
    function test_removed_row_failure_is_not_presented() {
        enable(true)
        const item = page(extensions)
        callbacks[0]([edition()])
        item.downloadEdition(edition())
        item.editions = []
        books.failed(edition().md5, "Retired row failure")
        compare(item.readError, "")
    }
    function test_newer_action_retires_explicit_failure_intent_data() {
        return [ { tag: "done edition", action: "done" },
                 { tag: "active edition", action: "active" },
                 { tag: "disabled edition", action: "disabled" },
                 { tag: "external row without link", action: "external" },
                 { tag: "torrent", action: "torrent" },
                 { tag: "local Read", action: "read" } ]
    }
    function test_newer_action_retires_explicit_failure_intent(data) {
        enable(true)
        const item = page(extensions)
        const newer = { md5: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", format: "epub" }
        callbacks[0]([edition(), newer])
        item.downloadEdition(edition())
        item.readError = "Previous message"
        if (data.action === "done" || data.action === "active") {
            books.state = data.action === "done" ? "done" : "downloading"
            item.downloadEdition(newer)
        } else if (data.action === "disabled") {
            enable(false)
            item.editions = [edition(), newer]
            item.downloadEdition(newer)
        } else if (data.action === "external") {
            item.downloadEdition({ source: "external" })
        } else if (data.action === "torrent") {
            item.downloadTorrent({ infoHash: "fixture-torrent" })
        } else {
            books.state = "done"
            item.readBook()
            compare(reads.count, 1)
        }
        const currentError = item.readError
        books.failed(edition().md5, "Obsolete A failure")
        compare(item.readError, currentError)
    }
    function test_old_book_callback_cannot_replace_new_book() {
        enable(true)
        const item = page(extensions)
        item.book = { title: "Second fixture", author: "Author" }
        compare(searches, 2)
        callbacks[0]([edition()])
        compare(item.editions.length, 0)
        callbacks[1]([edition()])
        compare(item.editions.length, 1)
    }
}
