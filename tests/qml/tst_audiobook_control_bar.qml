import QtQuick
import QtTest

TestCase {
    id: tc
    name: "AudiobookControlBar"
    width: 900; height: 360
    visible: true
    when: windowShown
    property var bar
    property var stub
    // Optional output directory for a standalone offscreen render probe; ordinary tests write nothing.
    property string renderDirectory: ""
    Component {
        id: sessionComponent
        QtObject {
            property int chapterIndex: 1
            property var chapterModel: [{label:"Opening"}, {label:"Unity"}, {label:"Next"}]
            property real chapterStart: 600
            property real chapterDuration: 1200
            property real chapterPosition: 328
            property real bookFraction: 0.27
            property bool canGoBack: true
            property bool paused: false
            property real speed: 1.25
            property real volume: 64
            property bool mute: false
            property int sleepMinutes: 30
            property int sleepMinutesLeft: 23
            property var calls: []
            function log(n, a) { calls = calls.concat([[n, a]]) }
            function togglePlay() { log("togglePlay"); paused = !paused }
            function markJumpOrigin() { log("markJumpOrigin") }
            function skipBook(d) { log("skipBook", d) }
            function seekInChapter(f) { log("seekInChapter", f) }
            function goToChapter(i) { log("goToChapter", i) }
            function backToPrevious() { log("backToPrevious") }
            function cycleSpeed() { log("cycleSpeed") }
        }
    }
    SignalSpy { id: preview; signalName: "scrubPreviewed" }
    SignalSpy { id: commit; signalName: "scrubCommitted" }
    SignalSpy { id: chapters; signalName: "chaptersRequested" }
    function make(compact, wide) {
        stub = createTemporaryObject(sessionComponent, tc)
        var c = Qt.createComponent("../../qml/audiobook/AudiobookControlBar.qml")
        compare(c.status, Component.Ready, c.errorString())
        bar = createTemporaryObject(c, tc, {session:stub, compact:!!compact, wide:!!wide, width:compact ? 380 : 780, x:30, y:150})
        verify(bar !== null)
        preview.target = bar; commit.target = bar; chapters.target = bar
        preview.clear(); commit.clear(); chapters.clear()
        waitForRendering(bar)
    }
    function f(n) { return findChild(bar, "abBar" + n) }
    function names() { return stub.calls.map(function(c) { return c[0] }).join() }
    function cleanup() { preview.target = null; commit.target = null; chapters.target = null; bar = null; stub = null }
    function test_compactControls() {
        make(true)
        ;["Back60", "Back10", "Play", "Fwd10", "Fwd60", "Undo", "ChapterLine"].forEach(function(n) { verify(f(n).visible, n) })
        ;["Speed", "Sleep", "PrevChapter", "NextChapter", "BookLine", "Volume", "Chapters"].forEach(function(n) { verify(!f(n).visible, n) })
        verify(bar.height <= 50)
        compare(f("CompactTime").text, "−14:32")
    }
    function test_compactHidesWhenAsleep() { make(true); bar.awake = false; tryCompare(bar, "opacity", 0); verify(!bar.visible) }
    function test_fullIgnoresAwake() { make(false); bar.awake = false; verify(bar.visible); compare(bar.opacity, 1) }
    function test_fullControls() {
        make(false)
        ;["Back60", "Back10", "Play", "Fwd10", "Fwd60", "Undo", "PrevChapter", "NextChapter", "Speed", "Sleep", "Volume", "Mute", "Chapters", "Time", "ChapterLine", "BookLine", "EndsAt"].forEach(function(n) { verify(f(n).visible, n) })
        compare(bar.height, 126)
    }
    function test_wideLayoutAndTimeMenu() {
        make(false, true)
        bar.bookTitle = "The Way of Kings"
        compare(bar.height, 104)
        verify(!f("Chapters").visible)
        verify(f("Cover").visible)
        compare(f("ChapterLabel").text, "Unity")
        compare(f("BookTitle").text, "The Way of Kings")
        var center = f("Play").mapToItem(bar, f("Play").width / 2, f("Play").height / 2)
        compare(center.x, bar.width / 2)
        verify(f("Speed").mapToItem(bar, 0, 0).x > center.x)
        verify(f("ChapterLabel").mapToItem(bar, 0, 0).x < center.x)
        verify(f("EndsAt").mapToItem(bar, 0, 0).y < 0)
        var rail = f("ChapterLine")
        verify(rail.width > bar.width * 0.8)
        compare(f("BookLine").width, rail.width)
        mouseClick(rail, rail.width * 0.25, rail.height / 2)
        compare(names(), "markJumpOrigin,seekInChapter")
        verify(Math.abs(stub.calls[1][1] - 0.25) < 0.01)
        mouseClick(rail, rail.width / 2, rail.height / 2, Qt.RightButton)
        tryCompare(f("Menu"), "opened", true)
        mouseClick(f("Menu").itemAt(0))
        compare(f("Time").text, "20:00")
        f("TimeAction").forceActiveFocus()
        keyClick(Qt.Key_Return)
        tryCompare(f("Menu"), "opened", true)
    }

    function test_wideVolumeCanBeOpenedFromKeyboard() {
        make(false, true)
        f("Mute").forceActiveFocus(Qt.TabFocusReason)
        tryCompare(f("VolumePopup"), "opened", true)
        compare(f("VolumeSlider").orientation, Qt.Horizontal)
        keyClick(Qt.Key_Up)
        compare(f("VolumeSlider").activeFocus, true)
        keyClick(Qt.Key_Home)
        compare(stub.volume, 0)
    }

    function test_skip60MarksFirst() { make(); mouseClick(f("Back60")); compare(names(), "markJumpOrigin,skipBook"); compare(stub.calls[1][1], -60) }
    function test_skip10DoesNotMark() { make(); mouseClick(f("Back10")); compare(names(), "skipBook"); compare(stub.calls[0][1], -10) }
    function test_forwardSkips() { make(); mouseClick(f("Fwd10")); mouseClick(f("Fwd60")); compare(names(), "skipBook,markJumpOrigin,skipBook"); compare(stub.calls[0][1], 10); compare(stub.calls[2][1], 60) }
    function test_play() { make(); mouseClick(f("Play")); compare(names(), "togglePlay") }
    function test_undo() { make(); stub.canGoBack = false; verify(!f("Undo").visible); stub.canGoBack = true; verify(f("Undo").visible); mouseClick(f("Undo")); compare(names(), "backToPrevious") }
    function test_chapterButtons() { make(); mouseClick(f("PrevChapter")); mouseClick(f("NextChapter")); compare(names(), "markJumpOrigin,goToChapter,markJumpOrigin,goToChapter"); compare(stub.calls[1][1], 0); compare(stub.calls[3][1], 2); stub.chapterIndex = 0; verify(!f("PrevChapter").enabled); stub.chapterIndex = 2; verify(!f("NextChapter").enabled) }
    function test_speedAndChapters() { make(); mouseClick(f("Speed")); compare(names(), "cycleSpeed"); mouseClick(f("Chapters")); compare(chapters.count, 1) }
    function test_sleepPresets() { make(); compare(f("Sleep").badge, "23m"); mouseClick(f("Sleep")); var menu = f("SleepMenu"); tryCompare(menu, "opened", true); compare(menu.count, 5); mouseClick(menu.itemAt(3)); compare(stub.sleepMinutes, 45); menu.close(); stub.sleepMinutesLeft = 0; compare(f("Sleep").badge, "") }
    function test_statusAndChapterClock() {
        make(); stub.chapterIndex = 11
        compare(f("ChapterLabel").text, "Audiobook")
        stub.chapterIndex = 1
        compare(f("ChapterLabel").text, "Unity")
        bar.nowMs = new Date(2026, 10, 18, 10, 0, 0).getTime()
        compare(f("EndsAt").text, "chapter ends " + Qt.formatTime(new Date(bar.nowMs + 872 / 1.25 * 1000), "h:mm AP"))
        stub.speed = 2
        compare(f("EndsAt").text, "chapter ends " + Qt.formatTime(new Date(bar.nowMs + 872 / 2 * 1000), "h:mm AP"))
    }
    function test_timeToggleAndContextMenu() {
        make(); compare(f("Time").text, "−14:32"); mouseClick(f("Time")); compare(f("Time").text, "20:00")
        mouseClick(bar, 300, 10, Qt.RightButton); tryCompare(f("Menu"), "opened", true); compare(f("Menu").count, 1)
        mouseClick(f("Menu").itemAt(0)); compare(f("Time").text, "−14:32"); f("Menu").close()
    }
    function test_scrubPreviewAndCommit() {
        make(); var rail = f("ChapterLine")
        mousePress(rail, rail.width * .2, rail.height / 2)
        mouseMove(rail, rail.width * .7, rail.height / 2)
        compare(stub.calls.length, 0); verify(preview.count >= 1)
        mouseRelease(rail, rail.width * .7, rail.height / 2)
        compare(commit.count, 1); compare(names(), "markJumpOrigin,seekInChapter"); verify(Math.abs(stub.calls[1][1] - .7) < .01)
    }
    function test_escapeCancelsScrub() { make(); var r = f("ChapterLine"); mousePress(r, 100, 10); keyClick(Qt.Key_Escape); mouseRelease(r, 200, 10); compare(stub.calls.length, 0); compare(commit.count, 0) }
    function test_chapterChangeCancelsScrub() { make(); var r = f("ChapterLine"); mousePress(r, 100, 10); stub.chapterIndex++; mouseRelease(r, 200, 10); compare(stub.calls.length, 0); compare(commit.count, 0) }
    function test_keyboardScrub() { make(); var r = f("ChapterLine"); r.forceActiveFocus(); keyClick(Qt.Key_Right); compare(stub.calls[1][1], 333/1200); keyClick(Qt.Key_Left); compare(stub.calls[3][1], 323/1200); keyClick(Qt.Key_Home); compare(stub.calls[5][1], 0); keyClick(Qt.Key_End); compare(stub.calls[7][1], 1) }
    function test_unknownDurationSafe() { make(); stub.chapterDuration = 0; compare(f("Time").text, "−0:00"); verify(!f("ChapterLine").enabled); verify(!f("EndsAt").visible) }
    function test_keyboardTraversal_data() { return [{tag:"full", compact:false}, {tag:"compact", compact:true}] }
    function test_keyboardTraversal(data) {
        make(data.compact)
        var controls = data.compact ? ["Back60", "Back10", "Play", "Fwd10", "Fwd60", "Undo"] : ["Time", "Speed", "Sleep", "PrevChapter", "Back60", "Back10", "Play", "Fwd10", "Fwd60", "NextChapter", "Undo", "Chapters", "Mute"]
        f(controls[0]).forceActiveFocus()
        var reached = ({})
        for (var i=0; i<40; ++i) {
            for (var j=0; j<controls.length; ++j) if (f(controls[j]).activeFocus) reached[controls[j]] = true
            keyClick(Qt.Key_Tab)
        }
        controls.forEach(function(n) { verify(reached[n], "Tab reaches " + n) })
    }
    function test_volumeHoverPopup() {
        make(); mouseMove(f("Mute"), 20, 20)
        tryCompare(f("VolumePopup"), "opened", true)
        mouseMove(bar, 300, 10)
        tryCompare(f("VolumePopup"), "opened", false)
    }
    function test_volumeMuteAndKeyboard() { make(); mouseClick(f("Mute")); compare(stub.mute, true); f("Mute").forceActiveFocus(); keyClick(Qt.Key_Up); tryCompare(f("VolumePopup"), "opened", true); var slider=f("VolumeSlider"); slider.forceActiveFocus(); keyClick(Qt.Key_Home); compare(stub.volume, 0); keyClick(Qt.Key_End); compare(stub.volume, 100); f("VolumePopup").close() }
    function test_renderBothSizes() {
        make(false)
        var chapters = []
        for (var i = 0; i < 13; ++i) chapters.push({label: i === 11 ? "Unity" : "Chapter " + (i + 1)})
        stub.chapterModel = chapters; stub.chapterIndex = 11
        stub.chapterDuration = 2612; stub.chapterPosition = 1740
        bar.nowMs = new Date(2026, 10, 18, 1, 2, 23).getTime()
        wait(100)
        var full = grabImage(bar)
        compare(full.width, 780); compare(full.height, 126)
        if (renderDirectory) full.save(renderDirectory + "/bar-full.png")
        bar.compact = true; bar.width = 380; waitForRendering(bar); wait(100)
        var compact = grabImage(bar)
        compare(compact.width, 380); compare(compact.height, 50)
        if (renderDirectory) compact.save(renderDirectory + "/bar-compact.png")
    }
}
