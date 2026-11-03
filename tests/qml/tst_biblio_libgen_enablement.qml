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
