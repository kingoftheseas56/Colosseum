import QtQuick
import QtTest
import "../../qml/audiobook/AudiobookListenPageData.js" as Data

// Slice E: the audiobook listening page, driven by a STUB session (AudiobookSession itself needs
// the C++ Player types and is not loadable by this runner). The stub mirrors the engine surface
// the page may touch and records every call.
TestCase {
    id: tc
    name: "AudiobookListenPage"
    width: 1280
    height: 720
    visible: true
    when: windowShown

    property var page: null
    property var stub: null

    Component {
        id: stubComponent
        QtObject {
            property bool ready: true
            property bool completed: false
            property var chapterModel: [
                { label: "Opening Credits", kind: "file", index: 0, time: 0 },
                { label: "Chapter One", kind: "file", index: 1, time: 0 },
                { label: "Chapter Two", kind: "file", index: 2, time: 0 }
            ]
            property int currentIndex: 0
            property real position: 65
            property real duration: 600
            readonly property int chapterIndex: {
                if (!chapterModel.length || chapterModel[0].kind === "file") return currentIndex
                var active = 0
                for (var i = 0; i < chapterModel.length; i++) {
                    if (chapterModel[i].time <= position) active = i
                }
                return active
            }
            readonly property real chapterStart: chapterModel.length && chapterModel[chapterIndex].kind === "seek"
                ? chapterModel[chapterIndex].time : 0
            readonly property real chapterPosition: position - chapterStart
            readonly property real chapterDuration: chapterModel.length && chapterModel[0].kind === "seek"
                && chapterIndex + 1 < chapterModel.length ? chapterModel[chapterIndex + 1].time - chapterStart
                : duration - chapterStart
            property real bookFraction: 0.27
            property bool canGoBack: false
            property real volume: 60
            property bool mute: false
            property int sleepMinutesLeft: 0
            property bool paused: true
            property real speed: 1.0
            property int sleepMinutes: 0
            property var calls: []
            function log(name, arg) { var c = calls.slice(); c.push([name, arg]); calls = c }
            function togglePlay() { log("togglePlay"); paused = !paused }
            function seekInChapter(fraction) { log("seekInChapter", fraction) }
            function markJumpOrigin() { log("markJumpOrigin") }
            function skipBook(d) { log("skipBook", d) }
            function goToChapter(i) { log("goToChapter", i); currentIndex = i }
            function cycleSpeed() { log("cycleSpeed"); speed = 1.25 }
            function callNames() { return calls.map(function (c) { return c[0] }) }
        }
    }
    function makePage(extraBook, sessionProps) {
        destroyPage()
        stub = stubComponent.createObject(tc)
        var props = sessionProps || ({})
        for (var k in props) stub[k] = props[k]
        var c = Qt.createComponent("../../qml/audiobook/AudiobookListenPage.qml")
        compare(c.status, Component.Ready, c.errorString())
        var book = { title: "The Long Way", authors: ["Becky Chambers"], narrators: ["Rachel Dulude"] }
        for (var b in (extraBook || ({}))) book[b] = extraBook[b]
        page = c.createObject(tc, { session: stub, book: book, width: 1280, height: 720 })
        verify(page !== null)
        waitForRendering(page)
        return page
    }
    function find(name) { return findChild(page, name) }
    function visibleNamed(name) { var o = find(name); return o !== null && o.visible }

    function destroyPage() {
        if (page) page.destroy()
        if (stub) stub.destroy()
        page = null
        stub = null
    }

    function cleanup() { destroyPage() }

    function test_titleAuthorAndNarratorShow() {
        makePage()
        var song = find("vinylSongTitle")
        verify(song !== null)
        compare(song.text, "The Long Way")
        var artist = find("vinylSongArtist")
        verify(artist !== null)
        verify(!visibleNamed("vinylSongAlbum"))
        compare(artist.names.join(" "), "Becky Chambers")
        var narrators = find("audiobookNarrators")
        verify(narrators.visible)
        compare(narrators.text, "Narrated by Rachel Dulude")
    }

    function test_noNarratorsMeansNoNarratorLine() {
        makePage({ narrators: [] })
        verify(!visibleNamed("audiobookNarrators"))
    }

    function test_bookTitleRemainsVisibleWithoutChapters() {
        makePage({}, { chapterModel: [] })
        compare(find("vinylSongTitle").text, "The Long Way")
        mouseClick(find("abBarChapters"))
        verify(!visibleNamed("audiobookChapters"))
    }

    function test_fullAudiobookBarReplacesVinylBar() {
        makePage()
        verify(visibleNamed("abBarSpeed"))
        compare(find("vinylPlayerBar"), null)
    }

    function test_wholeBookLineIsVisible() {
        makePage()
        verify(visibleNamed("abBarBookLine"))
    }

    function test_chaptersButtonOpensChapterList() {
        makePage()
        verify(!visibleNamed("audiobookChapters"))
        var button = find("abBarChapters")
        verify(button !== null)
        mouseClick(button)
        verify(visibleNamed("audiobookChapterList"))
        var panel = find("audiobookChapters")
        var poster = find("vinylSongPoster")
        verify(panel.x > poster.mapToItem(page, poster.width, 0).x)
        mouseClick(find("audiobookCloseChapters"))
        verify(!visibleNamed("audiobookChapters"))
    }

    function test_playPauseTogglesStub() {
        makePage()
        mouseClick(find("abBarPlay"))
        compare(stub.callNames().join(), "togglePlay")
        compare(stub.paused, false)
        mouseClick(find("abBarPlay"))
        compare(stub.callNames().join(), "togglePlay,togglePlay")
    }

    function test_keyboardSeekCallsSeekTo() {
        makePage()
        var seek = find("abBarChapterLine")
        verify(seek.enabled)
        seek.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(stub.callNames().join(), "markJumpOrigin,seekInChapter")
        compare(stub.calls[1][1], 70 / 600)
    }

    function test_pointerSeekCallsSeekTo() {
        makePage()
        var seek = find("abBarChapterLine")
        mouseClick(seek, seek.width / 2, seek.height / 2)
        compare(stub.callNames().join(), "markJumpOrigin,seekInChapter")
        compare(stub.calls[1][1], 0.5)
    }

    function test_nextAndPreviousJumpChaptersAndDisableAtEnds() {
        makePage()
        verify(!find("abBarPrevChapter").enabled, "first chapter: no previous")
        verify(find("abBarNextChapter").enabled)
        mouseClick(find("abBarNextChapter"))
        compare(stub.callNames().join(), "markJumpOrigin,goToChapter")
        compare(stub.calls[1][1], 1)
        verify(find("abBarPrevChapter").enabled)
        mouseClick(find("abBarNextChapter"))
        compare(stub.calls[3][1], 2)
        verify(!find("abBarNextChapter").enabled, "last chapter: no next")
        mouseClick(find("abBarPrevChapter"))
        compare(stub.calls[5][1], 1)
    }

    function test_chapterTapJumpsAndCurrentIsHighlighted() {
        makePage()
        mouseClick(find("abBarChapters"))
        var current = find("audiobookChapterHighlight_0").color
        var other = find("audiobookChapterHighlight_2").color
        verify(current.a > 0, "current chapter row is highlighted")
        compare(other.a, 0, "other rows are not")
        mouseClick(find("audiobookChapter_0"))
        compare(stub.calls.length, 0, "reselecting the current file does not record a jump")
        mouseClick(find("audiobookChapter_2"))
        compare(stub.callNames().join(), "markJumpOrigin,goToChapter")
        compare(stub.calls[1][1], 2)
        tryVerify(function () { return find("audiobookChapterHighlight_2").color.a > 0 })
        compare(find("audiobookChapterHighlight_0").color.a, 0)
    }

    function test_singleFileChapterUsesPositionAndShowsRealTimes() {
        makePage({}, {
            chapterModel: [{ label: "Intro", kind: "seek", index: 0, time: 0 },
                           { label: "Part 1", kind: "seek", index: 1, time: 120 },
                           { label: "Part 2", kind: "seek", index: 2, time: 400 }],
            position: 130
        })
        mouseClick(find("abBarChapters"))
        compare(find("vinylSongTitle").text, "The Long Way")
        compare(find("abBarChapterLabel").text, "CHAPTER 2 · PART 1")
        verify(!find("audiobookChapterTime_0").visible, "no start time shown when it is zero")
        compare(find("audiobookChapterTime_1").text, "NOW")
        compare(find("audiobookChapterTime_2").text, "6:40")
        stub.position = 410
        tryCompare(find("abBarChapterLabel"), "text", "CHAPTER 3 · PART 2")
        compare(find("audiobookChapterTime_1").text, "2:00")
    }

    function test_speedControlUsesSession() {
        makePage()
        mouseClick(find("abBarSpeed"))
        compare(stub.callNames().join(), "cycleSpeed")
        compare(find("abBarSpeed").label, "1.25×")
        stub.speed = 1.5
        compare(find("abBarSpeed").label, "1.5×")
    }

    function test_skipAndSleepUseSession() {
        makePage()
        mouseClick(find("abBarBack10"))
        mouseClick(find("abBarFwd10"))
        compare(stub.callNames().join(), "skipBook,skipBook")
        compare(stub.calls[0][1], -10)
        compare(stub.calls[1][1], 10)
        mouseClick(find("abBarSleep"))
        var menu = find("abBarSleepMenu")
        tryCompare(menu, "opened", true)
        mouseClick(menu.itemAt(1))
        compare(stub.sleepMinutes, 15)
    }

    function test_noDescriptionOrSeriesHidesNotesColumn() {
        makePage()
        verify(!visibleNamed("audiobookNotes"))
        makePage({ description: "A crew crosses the galaxy." })
        verify(visibleNamed("audiobookNotes"))
        compare(find("audiobookDescription").text, "A crew crosses the galaxy.")
        verify(!find("audiobookSeriesLine").visible)
        makePage({ seriesLine: "Wayfarers, book 1" })
        verify(visibleNamed("audiobookNotes"))
        compare(find("audiobookSeriesLine").text, "Wayfarers, book 1")
        verify(!find("audiobookDescription").visible)
    }

    function test_noVinylOnlyControlIsVisible() {
        makePage({ description: "x" })
        verify(!visibleNamed("vinylBarVideo"), "video tool")
        verify(!visibleNamed("vinylBarQueue"), "queue tool")
        verify(!visibleNamed("vinylBarSources"), "source picker")
        verify(!visibleNamed("vinylBarYouTube"))
        verify(!visibleNamed("vinylBarTankorent"))
        verify(!visibleNamed("vinylSongPageActions"), "song actions (save, playlist, lyrics, genius)")
        verify(!visibleNamed("vinylBarLyrics"))
        verify(!visibleNamed("vinylBarNotes"))
        verify(!visibleNamed("vinylSongAddToCollections"))
        verify(!visibleNamed("vinylLyricsColumn"))
        verify(!visibleNamed("vinylNotesColumn"))
        verify(!visibleNamed("vinylSongSources"))
        verify(!find("vinylSongAlbum").enabled, "no album page to open")
    }

    function test_backRequested() {
        makePage()
        var fired = 0
        page.backRequested.connect(function () { fired++ })
        mouseClick(find("vinylSongBack"))
        compare(fired, 1)
    }

    function test_bookMetadataAndChapterTimeMapping() {
        makePage({ title: "T", authors: ["A", "B"], narrators: ["N"] })
        var edition = page.pageData.playingEdition
        compare(edition.title, "T")
        compare(edition.artist, "A · B")
        compare(edition.narrators[0], "N")
        compare(page.pageData.artwork("C:\\books\\t.jpg"), "file:///C:/books/t.jpg")
        compare(page.pageData.artwork(""), "")
        compare(Data.formatTime(3725), "1:02:05")
        compare(Data.formatTime(65), "1:05")
        compare(Data.edition({}).artist, "")
    }
}
