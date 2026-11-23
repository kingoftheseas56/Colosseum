import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/BiblioApi.js" as BiblioApi
import "../../qml/AbbApi.js" as Abb

TestCase {
    id: suite
    name: "BiblioLibgenEnablement"
    Window { id: promptHost; width: 960; height: 640; visible: true }
    property int searches: 0
    property var callbacks: []
    property var originalSearch
    property var audioCallbacks: []
    property int audioSearches: 0
    property var originalAudio
    property var originalHash
    property var hashCallbacks: []
    SignalSpy { id: bridgeRoutes; signalName: "audiobookBridgeRequested" }
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
        property int downloads: 0
        property string state: "none"
        signal resultsReady(var rows)
        signal searchFinished()
        signal finished(string infoHash, string path)
        function search(title, author) { searches++ }
        function download(id, title, author) { downloads++ }
        function statusOf(id) { return { state: state } }
        function downloadedBooks() { return [] }
        function downloadedFiles() { return [] }
        function isDownloaded(id) { return state === "done" }
        function localFile(id) { return state === "done" ? "/fixture/torrent.epub" : "" }
    }
    SignalSpy { id: reads; signalName: "readRequested" }
    QtObject {
        id: audiobooks
        property int downloads: 0
        property string downloadedKey: ""
        signal resolving(string key)
        signal progress(string key, double received, double total)
        signal finished(string key, string path)
        signal failed(string key, string reason)
        signal removed(string key)
        function isDownloaded(key) { return key !== "" && key === downloadedKey }
        function statusOf(key) { return {state: isDownloaded(key) ? "done" : "none"} }
        function downloadAudiobook(key, hash, title, author, bookId, path) { downloads++ }
    }
    QtObject {
        id: audioSession
        property string activePairKey: ""
        property bool ready: false
        property bool paused: true
        property bool completed: false
        function openFor(key, book, startPaused) {
            activePairKey = key
            paused = startPaused
            ready = true
        }
        function togglePlay() { paused = !paused }
    }
    SignalSpy { id: extensionRoutes; signalName: "openExtensionsRequested" }
    function page(owner, host) {
        const item = createTemporaryObject(pageComponent, host || suite, {
            extensionsRef: owner, booksRef: books, bookTorrentsRef: torrents,
            audiobooksRef: audiobooks, audioSessionRef: audioSession,
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
        extensions.setRows([{ id: "colosseum.well.libgen", enabled: value },
                            { id: "colosseum.well.indexers", enabled: true },
                            { id: "colosseum.well.audiobookbay", enabled: true }])
    }
    function initTestCase() {
        originalSearch = BiblioApi.searchLibgen
        originalAudio = Abb.resolveAudiobook
        originalHash = Abb.fetchInfoHash
        Abb.fetchInfoHash = function(slug, done) { suite.hashCallbacks = suite.hashCallbacks.concat([done]) }
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
        Abb.fetchInfoHash = originalHash
    }
    function init() {
        extensions.setRows([])
        searches = 0; callbacks = []; audioCallbacks = []; audioSearches = 0; hashCallbacks = []
        books.downloads = 0; books.deletions = 0; books.state = "none"
        torrents.searches = 0; torrents.downloads = 0; torrents.state = "none"
        audiobooks.downloads = 0; audiobooks.downloadedKey = ""
        audioSession.activePairKey = ""; audioSession.ready = false; audioSession.paused = true
    }
    function test_audiobook_local_works_with_source_disabled_and_without_ebook() {
        audiobooks.downloadedKey = BiblioApi.pairKey("Offline fixture", "Author")
        const item = page(extensions)
        item.parent = promptHost.contentItem
        const status = findChild(item, "biblioAudiobookStatus")
        const action = findChild(item, "biblioAudiobookStatusAction")
        verify(status.visible)
        compare(status.text, "Downloaded")
        verify(action.visible)
        compare(action.text, "Listen")
        action.clicked()
        compare(audioSession.activePairKey, audiobooks.downloadedKey)
        compare(audioSession.paused, false)
        compare(action.text, "Pause")
        action.clicked()
        compare(audioSession.paused, true)
        compare(item.localPath, "")
        compare(audioSearches, 0)

        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        compare(audioSearches, 0)
        audiobooks.downloadedKey = ""
        audiobooks.removed(item.pairKey)
        compare(item.audioLocal, false)
        compare(audioSearches, 0)
    }
    function test_audiobook_lookup_failure_retries_then_shows_true_empty() {
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        const item = page(extensions)
        item.parent = promptHost.contentItem
        const status = findChild(item, "biblioAudiobookStatus")
        const action = findChild(item, "biblioAudiobookStatusAction")
        compare(audioSearches, 0)
        compare(action.text, "Find releases")
        action.clicked()
        compare(status.text, "Searching AudioBookBay…")
        audioCallbacks[0](null, "timeout")
        compare(status.text, "AudioBookBay unavailable")
        verify(action.visible)
        compare(action.text, "Retry")
        action.clicked()
        compare(audioSearches, 2)
        audioCallbacks[1]({rows: []})
        compare(status.text, "No audiobook found")
        verify(!action.visible)
    }
    function test_audiobook_download_completion_retires_pending_lookup() {
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        const item = page(extensions)
        item.startAudiobookLookup()
        audiobooks.downloadedKey = item.pairKey
        audiobooks.finished(item.pairKey, "/fixture/audio")
        audioCallbacks[0](null, "timeout")
        compare(item.audioLocal, true)
        compare(item.abLoading, false)
        compare(item.abLookupUnavailable, false)
        compare(audioSearches, 1)
    }
    function test_audiobookbay_missing_and_disabled_never_searches() {
        const item = page(extensions)
        compare(audioSearches, 0)
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: false }])
        item.loadEditions()
        compare(audioSearches, 0)
        compare(item.abLoading, false)
    }
    function test_audiobookbay_uses_shared_extensions_prompt() {
        const item = page(extensions)
        item.parent = promptHost.contentItem
        extensionRoutes.target = item
        extensionRoutes.clear()
        const action = findChild(item, "audiobookSourceExtensionsAction")
        verify(action !== null)
        verify(action.visible)
        action.triggered()
        compare(extensionRoutes.count, 1)
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        verify(!action.visible)
    }
    function test_audiobookbay_removal_discards_search_and_hash_callbacks() {
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        const item = page(extensions)
        item.startAudiobookLookup()
        compare(audioSearches, 1)
        audioCallbacks[0]({ rows: [{ slug: "fixture" }] })
        compare(item.abRows.length, 1)
        compare(hashCallbacks.length, 0, "release detail must wait for a selection")
        item.activateAudiobookRow(item.abRows[0])
        compare(hashCallbacks.length, 1)
        extensions.setRows([])
        hashCallbacks[0]({ infoHash: "late" })
        compare(audiobooks.downloads, 0)
        compare(item.abRows.length, 0)
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        compare(audioSearches, 2)
        extensions.setRows([])
        audioCallbacks[1]({ rows: [{ slug: "obsolete" }] })
        compare(item.abRows.length, 0)
        compare(hashCallbacks.length, 1)
    }
    function test_audiobookbay_disable_during_hash_lookup_allows_later_retry() {
        const row = { slug: "fixture" }
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        const item = page(extensions)
        item.startAudiobookLookup()
        audioCallbacks[0]({ rows: [row] })
        item.activateAudiobookRow(row)
        compare(hashCallbacks.length, 1)
        extensions.setRows([])
        hashCallbacks[0]({ infoHash: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" })
        compare(audiobooks.downloads, 0)
        extensions.setRows([{ id: "colosseum.well.audiobookbay", enabled: true }])
        audioCallbacks[1]({ rows: [row] })
        item.activateAudiobookRow(row)
        compare(hashCallbacks.length, 2)
        hashCallbacks[1]({ infoHash: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" })
        compare(audiobooks.downloads, 1)
    }
    function test_audiobook_bridge_line_can_be_activated_by_keyboard() {
        const item = page(extensions, promptHost.contentItem)
        const action = findChild(item, "biblioAudiobookBridge")
        verify(action !== null)
        verify(!action.visible)

        bridgeRoutes.target = item
        bridgeRoutes.clear()
        item.audiobookBridge = { available: true, workId: "catalogue-work", recordingId: "catalogue-recording",
                                narrators: "Narrator One and Narrator Two" }
        compare(action.accessibleName, "Also an audiobook · narrated by Narrator One and Narrator Two")
        verify(action.visible)
        compare(action.focusPolicy, Qt.TabFocus)
        promptHost.requestActivate()
        tryVerify(function() { return promptHost.active })
        action.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return action.activeFocus })
        keyClick(Qt.Key_Return)
        compare(bridgeRoutes.count, 1)
        compare(bridgeRoutes.signalArguments[0][0].workId, "catalogue-work")
        compare(bridgeRoutes.signalArguments[0][0].recordingId, "catalogue-recording")
    }
    function test_downloaded_and_legacy_audiobooks_offer_listen() {
        const item = page(extensions, promptHost.contentItem)
        const action = findChild(item, "biblioAudiobookBridge")
        verify(action !== null)
        bridgeRoutes.target = item
        bridgeRoutes.clear()

        item.audiobookBridge = { workId: "catalogue-work", recordingKey: "downloaded-recording" }
        compare(action.accessibleName, "Listen")
        verify(action.visible)
        verify(waitForRendering(action))
        mouseClick(action, action.width / 2, action.height / 2)
        compare(bridgeRoutes.count, 1)
        compare(bridgeRoutes.signalArguments[0][0].recordingKey, "downloaded-recording")

        item.audiobookBridge = { recordingKey: "legacy-title-author-key" }
        compare(action.accessibleName, "Listen")
        verify(action.visible)
        verify(waitForRendering(action))
        mouseClick(action, action.width / 2, action.height / 2)
        compare(bridgeRoutes.count, 2)
        compare(bridgeRoutes.signalArguments[1][0].recordingKey, "legacy-title-author-key")
    }
    function test_catalogue_recording_without_a_narrator_still_has_a_detail_link() {
        const item = page(extensions, promptHost.contentItem)
        const action = findChild(item, "biblioAudiobookBridge")
        verify(action !== null)
        item.audiobookBridge = { workId: "catalogue-work", needsChoice: true,
                                recordings: [{ recordingKey: "first" }, { recordingKey: "second" }] }
        verify(action.visible)
        compare(action.accessibleName, "Also an audiobook")
        item.audiobookBridge = { workId: "catalogue-work", narrators: [{ name: "Kate Reading" }, "Michael Kramer"] }
        compare(action.accessibleName, "Also an audiobook · narrated by Kate Reading, Michael Kramer")
    }
    function test_tankorent_missing_or_disabled_does_not_search() {
        const item = page(extensions)
        compare(torrents.searches, 0)
        extensions.setRows([{ id: "colosseum.well.indexers", enabled: false }])
        item.loadTorrents()
        compare(torrents.searches, 0)
        compare(item.torLoading, false)
    }
    function test_tankorent_toggle_discards_late_results_and_reenable_searches() {
        extensions.setRows([{ id: "colosseum.well.indexers", enabled: true }])
        const item = page(extensions)
        compare(torrents.searches, 1)
        extensions.setRows([])
        torrents.resultsReady([{ infoHash: "late", title: "Late result" }])
        compare(item.torrents.length, 0)
        compare(item.torLoading, false)
        extensions.setRows([{ id: "colosseum.well.indexers", enabled: true }])
        compare(torrents.searches, 2)
        torrents.resultsReady([{ infoHash: "current", title: "Current result" }])
        compare(item.torrents.length, 1)
    }
    function test_tankorent_missing_blocks_stale_new_downloads() {
        const item = page(extensions)
        const row = { infoHash: "fixture", title: "Fixture" }
        item.downloadTorrent(row)
        compare(torrents.downloads, 0)
        const gen = item._beginReadIntent()
        item._targetReadCandidate(item._candidate("torrent", row.infoHash, "EPUB", "", row), gen)
        compare(torrents.downloads, 0)
        verify(!item.pendingReadActive)
        compare(books.deletions, 0)
    }
    function test_tankorent_enabled_starts_acquisition() {
        extensions.setRows([{ id: "colosseum.well.indexers", enabled: true }])
        const item = page(extensions)
        item.downloadTorrent({ infoHash: "fixture", title: "Fixture" })
        compare(torrents.downloads, 1)
    }
    function test_tankorent_disable_keeps_existing_foreground_download() {
        extensions.setRows([{ id: "colosseum.well.indexers", enabled: true }])
        const item = page(extensions)
        const row = { infoHash: "fixture", title: "Fixture" }
        torrents.state = "downloading"
        const gen = item._beginReadIntent()
        verify(item._targetReadCandidate(item._candidate("torrent", row.infoHash, "EPUB", "", row), gen))
        extensions.setRows([])
        compare(item.pendingReadTransport, "torrent")
        torrents.state = "done"
        torrents.finished("fixture", "/fixture/torrent.epub")
        compare(reads.count, 1)
        compare(reads.signalArguments[0][0], "/fixture/torrent.epub")
        compare(torrents.downloads, 0)
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
        compare(audioSearches, 0)
        item.startAudiobookLookup()
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
        compare(item.abLoading, false)
        compare(audioSearches, 0)
        compare(torrents.searches, 1)
    }
    function test_disabled_cold_open_finds_local_copy_before_other_callbacks() {
        enable(false)
        books.state = "done"
        const item = page(extensions)
        compare(item.abLoading, false)
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
