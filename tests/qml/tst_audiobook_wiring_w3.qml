import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: tc
    name: "AudiobookWiringW3"
    when: windowShown

    readonly property string pair: "the way of kings|brandon sanderson"
    readonly property string first: pair + "#as:kramer"
    readonly property string second: pair + "#src:part2"
    readonly property var ebook: ({ title: "The Way of Kings", author: "Brandon Sanderson", path: "book.epub" })
    readonly property var chapters: [{ label: "Prologue", kind: "seek" },
        { label: "Chapter 1 — Stormblessed", kind: "seek" }, { label: "Chapter 2 — Honor", kind: "seek" }]
    readonly property var toc: [{ label: "Prologue", href: "prologue.xhtml" },
        { label: "Stormblessed", href: "storm.xhtml" }, { label: "Honor", href: "honor.xhtml" }]

    Window { id: surface; width: 1280; height: 720; visible: true }
    QtObject {
        id: web
        property bool personalStateSealed: false
        property int personalStateGeneration: 0
        signal personalStateAboutToChange()
        signal personalStateChanged()
        signal dictResult(var result)
        function bookKey(path) { return "ebook-path-key" }
        function bookmarksGet(id) { return [] }
        function annotationsGet(id) { return [] }
    }
    Component {
        id: pairingComponent
        QtObject {
            property int revision: 0
            property var saved: ({})
            function getPairing(id) { return saved }
            function savePairing(id, data) { saved = data; ++revision }
        }
    }
    Component {
        id: localComponent
        QtObject {
            property var rows: []
            property string preferred: ""
            signal removed(string key)
            signal finished(string key, string path)
            function activeDownloads() { return [] }
            function recordingsForWork(key) { return key === tc.pair ? rows : [] }
            function downloadedAudiobooks() { return rows }
            function isDownloaded(key) { return rows.some(row => row.recordingKey === key) }
            function localFiles(key) { return isDownloaded(key) ? ["01.mp3"] : [] }
            function localAudiobook(key) { return "/audio" }
            function setDefaultRecording(pair, key) { preferred = key; return true }
            function resolveRecordingKey(key) {
                if (isDownloaded(key)) return key
                if (key === tc.pair) return preferred || (rows.length === 1 ? rows[0].recordingKey : "")
                return ""
            }
        }
    }
    Component {
        id: catalogueComponent
        QtObject {
            property int revision: 0
            property bool known: true
            function search(title, limit) { return known ? [{ id: "way", title: tc.ebook.title }] : [] }
            function work(id) {
                return known && id === "way" ? { id: "way", title: tc.ebook.title,
                    authors: [tc.ebook.author], recordings: [{ id: "kramer", narrators: ["Kramer"] }] } : {}
            }
        }
    }
    Component {
        id: audioComponent
        QtObject {
            property bool ready: true
            property string activePairKey: tc.first
            property var book: ({ title: tc.ebook.title, author: tc.ebook.author })
            property var chapterModel: tc.chapters
            property int chapterIndex: 1
            property int currentIndex: 0
            property real position: 71
            property real duration: 123
            property real chapterPosition: 40
            property real chapterDuration: 52
            property real bookFraction: 0.3
            property bool paused: false
            property bool completed: false
            property real speed: 1
            property real volume: 60
            property bool mute: false
            property bool canGoBack: false
            property int sleepMinutes: 0
            property int sleepMinutesLeft: 0
            property var calls: []
            function openFor(key, metadata) { calls = calls.concat(["open:" + key]); book = metadata; activePairKey = key }
            function markJumpOrigin() { calls = calls.concat(["mark"]) }
            function goToChapterKeepState(index) { calls = calls.concat(["jump:" + index]); chapterIndex = index }
        }
    }
    Component { id: flowComponent; App.AudiobookFlow { width: 1280; height: 720 } }
    SignalSpy { id: findSpy; signalName: "findAudiobookRequested" }
    SignalSpy { id: readSpy; signalName: "readAlongRequested" }

    function row(key) {
        return { recordingKey: key, pairKey: pair, workId: "way", title: ebook.title,
                 narrators: [key === first ? "Kramer" : "Reading"],
                 sourceRef: key === second ? "way-of-kings-part-2" : "" }
    }

    function loadShell(localRows, known) {
        const xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../qml/reader2/ReaderShell.qml"), false)
        xhr.send()
        const source = xhr.responseText.replace(/\bReader2Bridge\b/g, "tc.bridge")
            .replace("ReaderPaper {", 'Item { objectName: "paperFixture"; signal paperEvent(string name, var p);'
                + 'property bool glueUp: false; property bool readerDebug; property bool covered;'
                + 'property bool automationFocused: false; property var destinations: [];'
                + 'function goTo(href) { destinations = destinations.concat([href]) } function scrub() {}')
        const shell = Qt.createQmlObject(source, surface.contentItem, Qt.resolvedUrl("../../qml/reader2/TestReaderW3.qml"))
        shell.width = surface.width
        shell.height = surface.height
        verify("audiobookDownloads" in shell, "ReaderShell must expose bridge service seams")
        shell.audiobookDownloads = createTemporaryObject(localComponent, tc, { rows: localRows })
        shell.audiobookCatalogue = createTemporaryObject(catalogueComponent, tc, { known: known !== false })
        shell.audioPairingStore = createTemporaryObject(pairingComponent, tc)
        shell.audioSession = createTemporaryObject(audioComponent, tc)
        shell.bookMeta = ebook
        return shell
    }
    property alias bridge: web
    function cleanup() {
        const shell = findChild(surface, "bookReaderShell")
        if (shell) shell.destroy()
        wait(0)
    }

    function test_reader_jump_is_once_and_requires_tap() {
        const shell = loadShell([row(first)])
        shell.openBook(ebook.path, first)
        shell.bookToc = toc
        shell.bookReady = true
        shell.currentTocIndex = 0
        tryCompare(shell, "followOn", true)
        const paper = findChild(shell, "paperFixture")
        compare(paper.destinations, [])
        compare(shell.audioSession.calls, [])
        verify(shell.readAlongJumpOffer.indexOf("Stormblessed") >= 0)
        shell.acceptReadAlongJump()
        compare(paper.destinations, ["storm.xhtml"])
        compare(shell.audioSession.calls, [])
        shell.acceptReadAlongJump()
        compare(paper.destinations.length, 1)
    }

    function test_mismatched_chapters_keep_reading_position_and_disable_follow() {
        const shell = loadShell([row(first)])
        shell.audioSession.chapterModel = [{ label: "Track 1", kind: "file" }]
        shell.openBook(ebook.path, first)
        shell.bookToc = toc
        shell.bookReady = true
        shell.currentTocIndex = 0
        verify(!shell.audioFollowAllowed)
        shell.openAudioPanel()
        verify(!findChild(shell, "readerFollowToggle").enabled)
        compare(findChild(shell, "readerFollowExplanation").text,
                "This recording's chapters don't line up with the book.")
        shell.setFollow(true)
        verify(!shell.followOn)
        compare(shell.readAlongJumpOffer, "")
        compare(findChild(shell, "paperFixture").destinations, [])
        compare(shell.audioSession.calls, [])
    }

    function test_audio_button_and_find_state() {
        const shell = loadShell([])
        shell.openBook(ebook.path)
        findSpy.target = shell
        const audioButton = findChild(shell, "readerAudioButton")
        verify(audioButton.available)
        shell.primaryAudiobookAction()
        compare(findSpy.count, 1)
        shell.openAudioPanel()
        verify(findChild(shell, "readerFindAudiobook").visible)
        shell.audiobookCatalogue.known = false
        ++shell.audiobookCatalogue.revision
        verify(!audioButton.available)
    }

    function test_which_narration_and_detach_attach() {
        const shell = loadShell([row(first), row(second)])
        shell.openBook(ebook.path)
        verify(shell.audioBridge.needsChoice)
        shell.openAudioPanel()
        verify(findChild(shell, "readerNarrationChoices").visible)
        compare(findChild(shell, "readerAudioRecording_1").label, "Part 2 · Reading")
        shell.selectAudioRecording(first)
        compare(shell.audiobookDownloads.preferred, first)
        compare(shell.audioPairingStore.saved.recordingKey, first)
        tryVerify(() => shell.audioAttached)
        shell.detachAudioRecording()
        verify(!shell.audioAttached)
        verify(shell.audioPairingStore.saved.detached)
        shell.openBook(ebook.path)
        verify(!shell.audioAttached)
        shell.selectAudioRecording(second)
        tryCompare(shell, "audioRecordingKey", second)
        tryCompare(shell.audioSession, "activePairKey", second)
    }

    function test_legacy_pairing_resolves_and_plays() {
        const shell = loadShell([Object.assign(row(pair), { workId: "" })], false)
        shell.audioPairingStore.savePairing("ebook-path-key", { pairKey: pair })
        shell.openBook(ebook.path)
        tryCompare(shell, "audioRecordingKey", pair)
        tryCompare(shell.audioSession, "activePairKey", pair)
    }

    function test_retired_source_opens_with_its_original_recording_key() {
        const key = pair + "#librivox:1480"
        const legacy = Object.assign(row(key), { workId: "", sourceKind: "librivox", sourceRef: "1480" })
        const flow = createTemporaryObject(flowComponent, surface.contentItem)
        flow.downloads = createTemporaryObject(localComponent, tc, { rows: [legacy] })
        flow.session = createTemporaryObject(audioComponent, tc)

        verify(flow.openListen(key))
        compare(flow.currentPage.route.recordingKey, key)
        compare(flow.session.calls, ["open:" + key])
        compare(flow.session.activePairKey, key)
    }

    function test_player_ownership_and_reverse_jump() {
        const flow = createTemporaryObject(flowComponent, surface.contentItem)
        flow.downloads = createTemporaryObject(localComponent, tc, { rows: [row(first)] })
        flow.session = createTemporaryObject(audioComponent, tc)
        flow.catalog = createTemporaryObject(catalogueComponent, tc)
        verify("ebookLibrary" in flow)
        flow.ebookLibrary = []
        verify(flow.openListen(first))
        verify(!flow.currentPage.ebookAvailable)
        readSpy.target = flow
        flow.ebookLibrary = [ebook]
        verify(flow.currentPage.ebookAvailable)
        flow.currentPage.readAlongRequested()
        compare(readSpy.count, 1)
        compare(flow.session.calls, [])
        const player = flow.currentPage
        verify(flow.openListen(first, row(first), { index: 2, label: "Honor" }))
        compare(flow.currentPage, player)
        compare(flow.session.calls, [])
        verify(player.listeningJumpOffer.indexOf("Honor") >= 0)
        player.acceptListeningJump()
        compare(flow.session.calls, ["mark", "jump:2"])
        player.acceptListeningJump()
        compare(flow.session.calls.length, 2)
    }

    function test_player_reuses_the_route_after_a_reader_recording_swap() {
        const flow = createTemporaryObject(flowComponent, surface.contentItem)
        flow.downloads = createTemporaryObject(localComponent, tc, { rows: [row(first), row(second)] })
        flow.session = createTemporaryObject(audioComponent, tc)
        verify(flow.openListen(first))
        const player = flow.currentPage
        flow.session.activePairKey = second
        verify(flow.openListen(second))
        compare(flow.currentPage, player)
        compare(player.route.recordingKey, second)
        flow.session.activePairKey = first
        flow.refreshPlayerRoute()
        compare(player.route.recordingKey, first)
        compare(flow.session.calls, [])
        flow.back()
        verify(!flow.active)
    }
}
