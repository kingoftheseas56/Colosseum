import QtQuick
import QtTest
import "../../qml" as Colosseum
import "../../qml/BiblioLibraryApi.js" as Api

TestCase {
    id: testCase
    name: "BiblioLibraryAudiobooks"
    when: windowShown

    Window { id: testWindow; width: 1280; height: 720; visible: true }
    Component { id: worldType; Colosseum.BiblioWorld { width: 1280; height: 720; featuredRows: [] } }

    Component {
        id: downloadType
        QtObject {
            property var rows: []
            signal finished(string pairKey, string path)
            signal removed(string pairKey)
            function downloadedAudiobooks() { return rows.map(row => Object.assign({}, row)) }
        }
    }

    SignalSpy { id: openSpy; signalName: "audiobookRequested" }
    SignalSpy { id: removeSpy; signalName: "removeRequested" }

    function setup() {
        testWindow.requestActivate()
        const world = createTemporaryObject(worldType, testWindow.contentItem)
        verify(world !== null)
        world.activeTab = "library"
        const page = world._libraryPageForTest
        verify("mediumFilter" in page, "The Biblio Library needs a medium filter")
        const book = { id: "ebook", title: "A Book", payload: { book: { author: "Author A" } }, addedAt: 100 }
        const saved = { id: "audiobook:w1", type: "audiobook", title: "Dune", addedAt: 300,
                        payload: { kind: "audiobook", workId: "w1", recordingId: "full-cast",
                                   author: "Frank Herbert", cover: "" } }
        const downloads = createTemporaryObject(downloadType, page)
        verify(downloads !== null)
        downloads.rows = [
            { recordingKey: "local-dune", pairKey: "dune|frank", audiosiloRecordingId: "full-cast",
              title: "Dune", author: "Frank Herbert", addedAt: 200 },
            { recordingKey: "local-emma", pairKey: "emma|jane", audiosiloRecordingId: "full-cast",
              title: "Emma", author: "Jane Austen", addedAt: 400 },
            { recordingKey: "local-gone", title: "Gone", missing: true }
        ]
        const positions = { "local-dune": { progress: 0.4, updatedAt: 900 },
                            "local-emma": { progress: 1, completed: true, updatedAt: 500 } }
        page.collectionStore = { revision: 0, items: function(world) { return [book, saved] } }
        page.downloads = downloads
        page.progressStore = {
            revision: 0,
            recent: function(kind) { return [] },
            get: function(kind, id) { return kind === "audiobook" ? positions[id] : null }
        }
        openSpy.target = world
        openSpy.clear()
        removeSpy.target = page
        removeSpy.clear()
        // The Library follows the shared featured area in the scrollable Biblio world.
        const flick = world.pageFlickable
        wait(50)
        const parked = Math.min(world.parkY(page), flick.contentHeight - flick.height)
        verify(parked > 0)
        world.parkRow(page)
        tryVerify(() => Math.abs(flick.contentY - parked) < 1)
        return { page, positions, downloads }
    }

    function test_medium_filter_labels_and_combines_with_search_sort_and_state() {
        const f = setup()
        const page = f.page
        compare(page.mediumFilter, "")
        tryCompare(page, "visibleCount", 3)
        compare(page.visibleRows.map(r => r.title), ["Emma", "Dune", "A Book"])
        const all = findChild(page, "biblioLibraryMedium_all")
        const book = findChild(page, "biblioLibraryMedium_book")
        const audio = findChild(page, "biblioLibraryMedium_audiobook")
        compare(all, null)
        verify(book && audio)
        compare([book.label, audio.label], ["Books", "Audiobooks"])
        mouseClick(book)
        compare(page.visibleRows.map(r => r.title), ["A Book"])
        mouseClick(book)
        compare(page.visibleCount, 3)
        mouseClick(audio)
        compare(page.visibleRows.map(r => r.title), ["Emma", "Dune"])
        compare(findChild(page, "abLibraryState_audiobook:w1").text, "Started")
        compare(findChild(page, "abLibraryState_audiobook:download:local-emma").text, "Finished")
        compare(findChild(page, "biblioLibraryCardTouch_audiobook:w1").width,
                findChild(page, "biblioLibraryCardTouch_audiobook:w1").height)
        const progress = findChild(page, "biblioLibraryProgress_audiobook:w1")
        verify(progress.visible)
        compare(progress.width, progress.parent.width * 0.4)
        page.stateFilter = "inProgress"
        compare(page.visibleRows.map(r => r.title), ["Dune"])
        page.stateFilter = ""
        page.query = "austen"
        compare(page.visibleRows.map(r => r.title), ["Emma"])
        page.query = ""
        page.sortMode = "lastRead"
        compare(page.visibleRows.map(r => r.title), ["Dune", "Emma"])
        mouseClick(audio)
        compare(page.visibleCount, 3)
    }

    function test_download_card_click_routes_work_and_legacy_download_separately() {
        const f = setup()
        const page = f.page
        tryCompare(page, "visibleCount", 3)
        const dune = findChild(page, "biblioLibraryCardTouch_audiobook:w1")
        verify(dune !== null)
        mouseClick(dune)
        compare(openSpy.count, 1)
        const canonical = openSpy.signalArguments[0][0]
        compare(canonical.workId, "w1")
        compare(canonical.recordingId, "full-cast")
        compare(canonical.recordingKey, "local-dune")
        compare(canonical.author, "Frank Herbert")
        const emma = findChild(page, "biblioLibraryCardTouch_audiobook:download:local-emma")
        verify(emma !== null)
        mouseClick(emma)
        compare(openSpy.count, 2)
        const legacy = openSpy.signalArguments[1][0]
        compare(legacy.workId, "")
        compare(legacy.recordingKey, "local-emma")
        compare(legacy.title, "Emma")
        page.handleCardAction(page.visibleRows[0], "remove")
        compare(removeSpy.count, 0)
    }

    function test_saved_item_survives_when_download_is_missing() {
        const f = setup()
        f.downloads.rows[0].missing = true
        f.downloads.removed("dune|frank")
        const historical = f.page.visibleRows.find(r => r.entry.id === "audiobook:w1")
        verify(historical !== undefined)
        compare(historical.downloaded, false)
        compare(historical.listeningState, "started")
        delete f.positions["local-dune"]
        f.page.progRev++
        const row = f.page.visibleRows.find(r => r.entry.id === "audiobook:w1")
        compare(row.listeningState, "new")
        compare(findChild(f.page, "abLibraryState_audiobook:w1").text, "New")
        f.page.handleCardAction(row, "detail")
        compare(openSpy.signalArguments[0][0].workId, "w1")
        f.page.handleCardAction(row, "remove")
        compare(removeSpy.count, 1)
        compare(removeSpy.signalArguments[0][0].id, "audiobook:w1")
    }

    function test_finished_download_appears_in_open_library() {
        const f = setup()
        f.downloads.rows.push({ recordingKey: "local-new", title: "New recording", addedAt: 450 })
        f.downloads.finished("new", "C:/downloads/new")
        compare(f.page.visibleRows.map(r => r.title), ["New recording", "Emma", "Dune", "A Book"])
        compare(findChild(f.page, "abLibraryState_audiobook:download:local-new").text, "New")
    }

    function test_listening_state_data() {
        return [
            { tag: "missing", record: null, state: "new" },
            { tag: "empty", record: {}, state: "new" },
            { tag: "zero", record: { progress: 0, resume: { position: 0, fileIndex: 0 } }, state: "new" },
            { tag: "position", record: { progress: 0, resume: { position: 12, fileIndex: 0 } }, state: "started" },
            { tag: "file", record: { progress: 0, resume: { position: 0, fileIndex: 1 } }, state: "started" },
            { tag: "mid", record: { progress: 0.4 }, state: "started" },
            { tag: "explicit-completion", record: { completed: true, progress: 0.4 }, state: "finished" },
            { tag: "completion-without-fraction", record: { completed: true, progress: 0 }, state: "finished" },
            { tag: "threshold", record: { progress: 0.98 }, state: "finished" },
            { tag: "below-threshold", record: { progress: 0.97 }, state: "started" },
            { tag: "full", record: { progress: 1 }, state: "finished" }
        ]
    }

    function test_listening_state(data) {
        compare(Api.listeningState(data.record), data.state)
    }
}
