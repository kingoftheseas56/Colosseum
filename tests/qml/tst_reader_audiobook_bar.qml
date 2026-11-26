import QtQuick
import QtQuick.Window
import QtCore
import QtTest
import "../../qml/reader2/Reader2Logic.js" as Logic

TestCase {
    id: tc
    name: "ReaderAudiobookBar"
    when: windowShown
    Window { id: win; width: 1280; height: 720; visible: true }
    property var shell
    property var chrome
    property var stub
    property alias bridge: fakeBridge
    QtObject {
        id: fakeBridge
        property bool personalStateSealed: false
        property int personalStateGeneration: 0
        signal personalStateAboutToChange()
        signal personalStateChanged()
        signal dictResult(var result)
    }
    property var audiobooks: ({ isDownloaded: function(k) { return true }, localFiles: function(k) { return [] }, downloadedAudiobooks: function() { return [] } })
    Component {
        id: sessionComponent
        QtObject {
            property bool ready: true
            property string activePairKey: "test-recording"
            property var book: ({ title: "The Way of Kings" })
            property var chapterModel: [{label:"One"}, {label:"Unity"}, {label:"Three"}]
            property int currentIndex: 1
            property int chapterIndex: currentIndex
            property real position: 928
            property real duration: 3000
            property real chapterPosition: 328
            property real chapterDuration: 1200
            property bool paused: true
            property real speed: 1
            property real volume: 60
            property bool mute: false
            property bool canGoBack: true
            property var jumpHistory: []
            property var calls: []
            function log(n, a) { calls = calls.concat([[n,a]]) }
            function markJumpOrigin() { log("mark"); jumpHistory = jumpHistory.concat([{fileIndex:currentIndex, position:position}]) }
            function seekInChapter(f) { log("seek", f) }
            function goToChapter(i) { log("chapter", i); currentIndex = i }
            function goToChapterKeepState(i) { log("follow", i); currentIndex = i }
            function cycleSpeed() { log("speed"); speed = speed === 1 ? 1.25 : 1 }
            function togglePlay() { paused = !paused }
            function skipBook(s) { log("skip", s) }
            function backToPrevious() {}
            function openFor() { log("open") }
            function names() { return calls.map(function(c) { return c[0] }) }
        }
    }
    // The generic QuickTest runner has no native Reader2Bridge or WebEngine startup.
    // Load the production shell unchanged except dependency injection at those boundaries;
    // all chrome, panel, and shell audio handlers below are the real production code.
    function init() {
        win.requestActivate()
        stub = sessionComponent.createObject(tc)
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../qml/reader2/ReaderShell.qml"), false)
        xhr.send()
        var source = xhr.responseText.replace(/Reader2Bridge/g, "tc.bridge")
            .replace(/\bAudiobooks\b/g, "tc.audiobooks")
            .replace("ReaderPaper {", 'Item { signal paperEvent(string name, var p); property bool glueUp: false; property bool readerDebug; property bool covered; property bool automationFocused: false; function setAppearance(a) {} function focusPaper() {} function clearSearch() {} function scrub() {} Rectangle { anchors.fill: parent; color: "#eee5d4" } Text { anchors.centerIn: parent; width: 620; text: "12  Unity\\n\\nThe storm had passed in the night, leaving the stone washed clean and the air cold enough to sting. From the terrace, the distant hills almost looked blue beneath the thinning clouds.\\n\\nHe stood for a time without speaking. Below him, the city stirred awake: the muted clang of metal, hurried footsteps, the first voices rising from the market. The world moved forward whether he understood it or not."; wrapMode: Text.WordWrap; font.family: "Georgia"; font.pixelSize: 22; lineHeight: 1.5; color: "#29231c" }')
        shell = Qt.createQmlObject(source, win.contentItem, Qt.resolvedUrl("../../qml/reader2/TestReaderShell.qml"))
        verify(shell !== null)
        shell.width = 1280; shell.height = 720
        shell.audioSession = stub
        shell.audioPairing = { pairKey: "test-work", recordingKey: "test-recording" }
        shell.audioEntry = { title: "The Way of Kings", fileCount: 3 }
        shell.bookTitle = "The Way of Kings"; shell.bookAuthor = "Brandon Sanderson"
        chrome = findChild(shell, "readerChrome")
        // Before Task 3 there is no named chrome seam; identify it by its public API.
        if (!chrome) for (var i = 0; i < shell.children.length; i++)
            if (shell.children[i].audioAttached !== undefined) { chrome = shell.children[i]; break }
        verify(chrome !== null && chrome !== undefined)
        chrome.wake()
        wait(200)
    }
    function cleanup() { if (shell) shell.destroy(); if (stub) stub.destroy(); shell = null; chrome = null; stub = null; wait(0) }
    function named(n) { return findChild(shell, n) }
    function panel() { chrome.openPanelTo("audio", "contents"); wait(350) }
    function test_attachedSlimBar() {
        verify(named("abBarPlay") !== null)
        // The bar fades in on the reader's awake beat; wait for it rather than sampling once.
        tryVerify(function() { return named("abBarPlay").visible }, 3000, "slim bar becomes visible")
        // The shared bar keeps its full-size controls as hidden instances in compact mode;
        // what matters is that the reader never shows them (Claude, 2026-11-18).
        var speed = named("abBarSpeed")
        verify(speed === null || !speed.visible, "speed control must not be visible in the reader")
    }
    function test_attachmentGating() {
        verify(named("abBarPlay") !== null)
        chrome.audioAttached = false
        wait(200)
        compare(named("abBarPlay"), null)
    }
    function test_geometry_data() { return [{tag:"single", mode:"single"}, {tag:"two", mode:"double"}, {tag:"scroll", mode:"scroll"}] }
    function test_geometry(data) {
        shell.appearance = Logic.appearanceDefaults()
        var appearance = Object.assign({}, shell.appearance)
        appearance.flow = data.mode === "scroll" ? "scroll" : "paginated"
        appearance.columns = data.mode === "double" ? "double" : "single"
        shell.appearance = appearance
        var bar = named("readerAudiobookBar"), rail = named("readerBottomRail")
        verify(bar !== null && rail !== null)
        compare(bar.height, 50)
        compare(bar.width, 380)
        verify(bar.mapToItem(chrome, 0, bar.height).y <= rail.y)
    }
    function test_fadesWithAwake() {
        var bar = named("readerAudiobookBar")
        verify(bar !== null)
        chrome.toggle()
        tryCompare(bar, "opacity", 0)
        chrome.wake()
        tryCompare(bar, "opacity", 1)
    }
    function test_scrubPreviewsThenCommits() {
        panel()
        var rail = named("audioChapterRail")
        verify(rail !== null)
        var previews = 0, commits = 0
        chrome.audioScrubPreviewed.connect(function(f) { previews++ })
        chrome.audioScrubCommitted.connect(function(f) { commits++ })
        mousePress(rail, rail.width * .2, rail.height / 2)
        mouseMove(rail, rail.width * .7, rail.height / 2, 30)
        verify(previews >= 1); compare(stub.calls.length, 0)
        mouseRelease(rail, rail.width * .7, rail.height / 2)
        compare(commits, 1); compare(stub.names().join(","), "mark,seek")
        fuzzyCompare(stub.calls[1][1], .7, .02)
    }
    function test_escapeCancelsScrub() {
        panel()
        var rail = named("audioChapterRail"); verify(rail !== null)
        mousePress(rail, rail.width * .3, rail.height / 2)
        keyClick(Qt.Key_Escape)
        mouseRelease(rail, rail.width * .8, rail.height / 2)
        compare(stub.calls.length, 0)
    }
    function test_chapterChangeCancelsScrub() {
        panel()
        var rail = named("audioChapterRail"); verify(rail !== null)
        mousePress(rail, rail.width * .3, rail.height / 2)
        stub.currentIndex = 2
        mouseRelease(rail, rail.width * .8, rail.height / 2)
        compare(stub.calls.length, 0)
    }
    function test_followNeverMarks() {
        shell.followOn = true
        shell.bookToc = [{label:"One"}, {label:"Unity"}, {label:"Three"}]
        shell.currentTocIndex = 2
        shell.syncAudioToPage(true)
        compare(stub.jumpHistory.length, 0)
        compare(stub.names().join(","), "follow")
    }
    function test_playlistMarksOnce() {
        panel()
        var row = named("audioPlaylistChapter_2"); verify(row !== null)
        mouseClick(row, row.width / 2, row.height / 2)
        compare(stub.names().join(","), "mark,chapter")
    }
    function test_speedUsesSession() {
        panel()
        var speed = named("audioPanelSpeed"); verify(speed !== null)
        mouseClick(speed, speed.width / 2, speed.height / 2)
        compare(stub.speed, 1.25)
        compare(named("audioPanelSpeedLabel").text, Logic.speedLabel(stub.speed))
        compare(shell.audioChosenSpeed, stub.speed)
    }
    function test_chaptersVolumeMute() {
        panel()
        var prev = named("audioPanelPrevChapter"), next = named("audioPanelNextChapter")
        var volume = named("audioPanelVolume"), mute = named("audioPanelMute")
        verify(prev && next && volume && mute)
        mouseClick(prev, prev.width/2, prev.height/2)
        mouseClick(next, next.width/2, next.height/2)
        compare(stub.names().join(","), "mark,chapter,mark,chapter")
        mouseClick(volume, volume.width*.3, volume.height/2)
        fuzzyCompare(stub.volume, 30, 2)
        mouseClick(mute, mute.width/2, mute.height/2)
        compare(stub.mute, true)
    }
    function test_renderEvidence() {
        var bar = named("readerAudiobookBar"); verify(bar !== null)
        const captureUrl = Qt.resolvedUrl(StandardPaths.writableLocation(StandardPaths.TempLocation)
                                          + "/colosseum-reader-audiobook-bar-" + Date.now() + ".png")
        var saved = false
        verify(win.contentItem.grabToImage(function(result) {
            saved = result.saveToFile(captureUrl)
        }))
        tryVerify(function() { return saved }, 5000, "Could not save reader bar capture to " + captureUrl)
    }
}
