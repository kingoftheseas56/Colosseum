import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "AudiobookWiringW2"
    when: windowShown

    readonly property string exactHash: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    readonly property string probableHash: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    readonly property string unknownHash: "cccccccccccccccccccccccccccccccccccccccc"
    readonly property string bundleHash: "dddddddddddddddddddddddddddddddddddddddd"
    readonly property string recordingKey: "the way of kings|brandon sanderson#as:kramer"

    Window { id: surface; width: 1280; height: 720; visible: true }
    Component { id: flowType; App.AudiobookFlow { width: 1280; height: 720 } }
    Component {
        id: worldType
        QtObject {
            signal audiobookRequested(var item)
            signal audiobookGenreRequested(string genre)
            signal audiobookShelfRequested(string key)
        }
    }
    Component {
        id: catalogueType
        QtObject {
            function work(id) {
                if (id !== "the-way-of-kings") return {}
                return { id: id, title: "The Way of Kings", authors: [{ id: "brandon", name: "Brandon Sanderson" }],
                         series: [{ id: "stormlight", name: "The Stormlight Archive", numberText: "1" }],
                         recordings: [
                             { id: "kramer", language: "en", abridgement: 0, narrators: [{ name: "Michael Kramer" }] },
                             { id: "cast", language: "en", abridgement: 0, narrators: [{ name: "Full Cast" }] }
                         ] }
            }
            function series(id) { return { works: [] } }
        }
    }
    Component {
        id: extensionsType
        QtObject {
            property bool enabled: true
            property int revision: 0
            function installed() { return [{ id: "colosseum.well.audiobookbay", enabled: enabled }] }
        }
    }
    Component {
        id: abbType
        QtObject {
            property var rows: []
            property var onSearchDone: null
            property bool delaySearch: false
            property int requests: 0
            property int cancellations: 0
            function searchTerm(title, author) { return title + " " + author }
            function relevantRows(items, title, author) { return items }
            function search(term, page, done) {
                ++requests
                if (delaySearch) onSearchDone = done
                else done({ rows: rows, page: 1, hasMore: false })
                return { cancel: () => { ++cancellations; onSearchDone = null } }
            }
            function detail(slug, done) {
                const item = rows.find(row => row.slug === slug)
                done({ infoHash: item.hash, narrator: item.narrators, language: "English",
                       format: item.fileCount === 1 ? "M4B" : "MP3", fileCount: item.fileCount,
                       abridgement: "unabridged", bundle: item.bundle, bundleCount: item.bundleCount })
                return { cancel: () => {} }
            }
        }
    }
    Component {
        id: matcherType
        QtObject {
            function matchSource(source, recordings) {
                if (source.infoHash === testCase.exactHash)
                    return { verdict: "exact", recordingKey: testCase.recordingKey }
                if (source.infoHash === testCase.probableHash)
                    return { verdict: "probable", recordingKey: testCase.recordingKey }
                return { verdict: "none", reason: "conflict" }
            }
        }
    }
    Component {
        id: downloadsType
        QtObject {
            property var jobs: ({})
            property var requests: []
            property var owned: []
            property var failureRows: []
            property int activeCount: 0
            signal resolving(string key)
            signal progress(string key, real received, real total)
            signal finished(string key, string path)
            signal failed(string key, string reason)
            signal removed(string key)
            signal failuresChanged()

            function statusOf(key) { return jobs[key] || { state: "none" } }
            function isDownloaded(key) { return statusOf(key).state === "done" }
            function downloadedAudiobooks() { return owned }
            function recordingsForWork(pairKey) { return owned.filter(record => record.pairKey === pairKey) }
            function activeDownloads() {
                return requests.filter(row => statusOf(row.recordingKey).state !== "done").map(row =>
                    Object.assign({}, row, statusOf(row.recordingKey))).concat(failureRows)
            }
            function downloadRecording(row) {
                requests = requests.concat([row])
                jobs[row.recordingKey] = { state: "resolving" }
                ++activeCount
                resolving(row.recordingKey)
            }
            function move(key, received, total) {
                jobs[key] = { state: "downloading", received: received, total: total }
                progress(key, received, total)
            }
            function complete(key, row) {
                jobs[key] = { state: "done" }
                owned = owned.concat([Object.assign({ recordingKey: key }, row || {})])
                activeCount = Math.max(0, activeCount - 1)
                finished(key, "/test/audio")
            }
            function fail(key) {
                jobs[key] = { state: "none" }
                activeCount = Math.max(0, activeCount - 1)
                failed(key, "Delivery failed")
            }
        }
    }
    Component {
        id: sessionType
        QtObject {
            property bool ready: false
            property string activePairKey: ""
            property var book: ({})
            property var opens: []
            property var chapterModel: []
            property int chapterIndex: -1
            property int currentIndex: 0
            property bool completed: false
            property real position: 0
            property real duration: 300
            property real chapterPosition: 0
            property real chapterDuration: 300
            property real bookFraction: 0
            property bool canGoBack: false
            property real volume: 60
            property bool mute: false
            property int sleepMinutesLeft: 0
            property int sleepMinutes: 0
            property bool paused: false
            property real speed: 1
            function openFor(key, metadata) {
                opens = opens.concat([key])
                activePairKey = key
                book = metadata
                ready = true
            }
        }
    }

    SignalSpy { id: extensionsSpy; signalName: "openExtensionsRequested" }

    function make(component, properties) {
        const object = createTemporaryObject(component, surface.contentItem, properties || {})
        verify(object !== null)
        return object
    }

    function upload(hash, overrides) {
        return Object.assign({ slug: "upload-" + hash[0], hash: hash, title: "The Way of Kings",
            author: "Brandon Sanderson", narrators: ["Michael Kramer"], format: "M4B", size: "1.5 GB",
            fileCount: 1, seeders: 10, bundle: false }, overrides || {})
    }

    function setup(rows, enabled) {
        const flow = make(flowType)
        verify("abb" in flow && "matcher" in flow && "downloads" in flow && "session" in flow,
               "W2 needs the real flow wired to source, matcher, downloads and playback")
        const abb = make(abbType, { rows: rows || [] })
        const downloads = make(downloadsType)
        const session = make(sessionType)
        const world = make(worldType)
        flow.catalog = make(catalogueType)
        flow.extensions = make(extensionsType, { enabled: enabled !== false })
        flow.abb = abb
        flow.matcher = make(matcherType)
        flow.downloads = downloads
        flow.session = session
        flow.world = world
        extensionsSpy.target = flow
        extensionsSpy.clear()
        return { flow: flow, abb: abb, downloads: downloads, session: session, world: world }
    }

    function book(view) {
        view.world.audiobookRequested({ workId: "the-way-of-kings", recordingId: "kramer" })
        const page = view.flow.currentPage
        compare(page.objectName, "audiobookBookPage")
        return page
    }

    function sources(view) {
        const current = view.flow.currentPage
        compare(current.objectName, "audiobookSourcesPage")
        return current
    }

    function rows(view, count) {
        const page = sources(view)
        tryVerify(() => page.providerStates.abb.rows.length === count
            && page.providerStates.abb.state !== "searching")
        return page.providerStates.abb.rows
    }

    function pick(view) {
        const result = rows(view, 1)[0]
        sources(view).sourcePicked(result)
    }

    function press(page) { findChild(page, "audiobookPrimaryAction").triggered() }

    function test_download_navigates_before_a_slow_provider() {
        const view = setup([upload(exactHash)])
        const detail = book(view)
        view.abb.delaySearch = true
        press(detail)
        compare(view.flow.currentPage.objectName, "audiobookSourcesPage")
        compare(view.abb.requests, 0)
        compare(view.downloads.requests.length, 0)
        tryCompare(view.abb, "requests", 1)
        view.flow.back()
        compare(view.flow.currentPage, detail)
        compare(view.abb.cancellations, 1)
    }

    function test_single_confident_match_waits_for_choice() {
        const view = setup([upload(exactHash)])
        const detail = book(view)
        press(detail)
        rows(view, 1)
        compare(view.downloads.requests.length, 0)
        pick(view)
        compare(view.downloads.requests.length, 1)
        compare(view.downloads.requests[0].recordingKey, recordingKey)
        compare(view.flow.currentPage, detail)
    }

    function test_two_matches_recommend_exact_without_downloading() {
        const view = setup([upload(probableHash, { seeders: 500 }), upload(exactHash, { fileCount: 4 })])
        press(book(view))
        const choices = rows(view, 2)
        compare(view.downloads.requests.length, 0)
        verify(choices[0].recommended)
        compare(choices[0].verdict, "exact")
        compare(choices.filter(row => row.recommended).length, 1)
    }

    function test_uncertain_upload_reports_reason_and_requires_a_tap() {
        const view = setup([upload(unknownHash, { narrators: ["Another Narrator"] })])
        press(book(view))
        verify(rows(view, 1)[0].reason.length > 0)
        compare(view.downloads.requests.length, 0)
        pick(view)
        compare(view.downloads.requests.length, 1)
        verify(view.downloads.requests[0].recordingKey.endsWith("#src:" + unknownHash))
    }

    function test_bundle_is_labelled_and_never_automatic() {
        const view = setup([upload(bundleHash, { bundle: true, bundleCount: 3 })])
        press(book(view))
        const row = rows(view, 1)[0]
        compare(view.downloads.requests.length, 0)
        verify(row.bundle)
        compare(row.bundleCount, 3)
    }

    function test_empty_search_reports_no_matches() {
        const view = setup([])
        press(book(view))
        rows(view, 0)
        compare(sources(view).providerStates.abb.state, "empty")
        compare(view.downloads.requests.length, 0)
    }

    function test_disabled_source_still_opens_sources() {
        const view = setup([upload(exactHash)], false)
        const detail = book(view)
        compare(detail.primary.label, "Download")
        press(detail)
        const page = sources(view)
        compare(page.providerStates.abb.state, "disabled")
        compare(page.providerStates.librivox.state, "disabled")
        compare(extensionsSpy.count, 0)
        compare(view.abb.requests, 0)
        findChild(page, "audiobookSourcesExtensions").clicked()
        compare(extensionsSpy.count, 1)
    }

    function test_back_ignores_old_result_and_error_can_retry() {
        const view = setup([upload(exactHash)])
        const detail = book(view)
        view.abb.delaySearch = true
        press(detail)
        tryCompare(view.abb, "requests", 1)
        const late = view.abb.onSearchDone
        view.flow.back()
        compare(view.flow.currentPage, detail)
        compare(view.abb.cancellations, 1)
        press(detail)
        tryCompare(view.abb, "requests", 2)
        late({ rows: view.abb.rows, page: 1, hasMore: false })
        compare(sources(view).providerStates.abb.state, "searching")
        view.abb.onSearchDone(null, "timeout")
        tryVerify(() => sources(view).providerStates.abb.state === "error")
        compare(sources(view).providerStates.abb.error, "timeout")
        view.abb.delaySearch = false
        sources(view).retryRequested("abb")
        pick(view)
        compare(view.downloads.requests.length, 1)
    }

    function test_download_progress_completion_and_listen_route() {
        const view = setup([upload(exactHash)])
        const detail = book(view)
        press(detail)
        pick(view)
        compare(view.downloads.requests.length, 1)
        view.downloads.move(recordingKey, 43, 100)
        compare(detail.primary.label, "Downloading 43%")
        compare(findChild(detail, "audiobookLocalStatus").text, "")
        view.downloads.complete(recordingKey, view.downloads.requests[0])
        compare(detail.primary.label, "Listen")
        press(detail)
        compare(view.session.opens, [recordingKey])
        const player = view.flow.currentPage
        compare(player.objectName, "audiobookListenPage")
        compare(player.book.title, "The Way of Kings")
        compare(player.book.seriesLine, "The Stormlight Archive · Book 1")
        compare(player.book.narrators, ["Michael Kramer"])
        compare(findChild(player, "audiobookPlayerSeries").text, "The Stormlight Archive · Book 1")
        view.flow.back()
        compare(view.flow.currentPage, detail)
    }

    function test_double_press_and_failure_retry_require_a_choice() {
        const view = setup([upload(exactHash)])
        const detail = book(view)
        press(detail)
        press(detail)
        rows(view, 1)
        compare(view.abb.requests, 1)
        pick(view)
        compare(view.downloads.requests.length, 1)
        view.downloads.fail(recordingKey)
        compare(detail.primary.label, "Retry")
        press(detail)
        rows(view, 1)
        compare(view.downloads.requests.length, 1)
        pick(view)
        compare(view.downloads.requests.length, 2)
        compare(view.abb.requests, 2)
    }

    function test_reopened_detail_uses_native_failure_snapshot() {
        const view = setup([upload(exactHash)])
        view.downloads.failureRows = [{ id: recordingKey, workId: "the-way-of-kings", state: "failed" }]
        view.downloads.failuresChanged()
        const detail = book(view)
        compare(detail.primary.label, "Retry")
        press(detail)
        pick(view)
        compare(view.downloads.requests.length, 1)
        compare(detail.primary.label, "Downloading")
    }

    function test_library_routes_downloads_and_bundles_to_player() {
        const view = setup([])
        const local = { workId: "the-way-of-kings", recordingId: "kramer", recordingKey: recordingKey,
                        title: "Local title", downloaded: true }
        view.downloads.complete(recordingKey, local)
        view.world.audiobookRequested(local)
        compare(view.flow.currentPage.objectName, "audiobookListenPage")
        compare(view.flow.currentPage.book.title, "The Way of Kings")
        compare(view.session.opens, [recordingKey])
        view.flow.back()
        compare(view.flow.active, false)
        view.world.audiobookRequested({ workId: local.workId, recordingId: "cast", downloaded: false })
        compare(view.flow.currentPage.objectName, "audiobookBookPage")
        view.flow.back()
        const key = "bundle#src:" + bundleHash
        const bundle = { recordingKey: key, title: "The Fantasy Bundle", bundle: true, downloaded: true }
        view.downloads.complete(key, bundle)
        view.world.audiobookRequested(bundle)
        compare(view.flow.currentPage.objectName, "audiobookListenPage")
        compare(view.flow.currentPage.book.title, bundle.title)
        compare(view.flow.currentPage.ebookAvailable, false)
        compare(view.session.opens, [recordingKey, key])
    }
}
