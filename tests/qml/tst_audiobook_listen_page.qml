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
    height: 700
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
            property bool paused: true
            property real speed: 1.0
            property int sleepMinutes: 0
            property var calls: []
            function log(name, arg) { var c = calls.slice(); c.push([name, arg]); calls = c }
            function togglePlay() { log("togglePlay"); paused = !paused }
            function seekTo(t) { log("seekTo", t) }
            function seekRel(d) { log("seekRel", d) }
            function goToChapter(i) { log("goToChapter", i); currentIndex = i }
            function setRate(r) { log("setRate", r); speed = r }
            function callNames() { return calls.map(function (c) { return c[0] }) }
        }
    }
    function makePage(extraBook, sessionProps) {
        stub = createTemporaryObject(stubComponent, tc)
        var props = sessionProps || ({})
        for (var k in props) stub[k] = props[k]
        var c = Qt.createComponent("../../qml/audiobook/AudiobookListenPage.qml")
        compare(c.status, Component.Ready, c.errorString())
        var book = { title: "The Long Way", authors: ["Becky Chambers"], narrators: ["Rachel Dulude"] }
        for (var b in (extraBook || ({}))) book[b] = extraBook[b]
        page = createTemporaryObject(c, tc, { session: stub, book: book, width: 1280, height: 700 })
        verify(page !== null)
        waitForRendering(page)
        return page
    }
    function find(name) { return findChild(page, name) }
    function visibleNamed(name) { var o = find(name); return o !== null && o.visible }

    function cleanup() { page = null; stub = null }

    function test_titleAuthorAndNarratorShow() {
        makePage()
        var song = find("vinylSongTitle")
        verify(song !== null)
        compare(song.text, "Opening Credits", "current chapter title is the track title")
        var artist = find("vinylSongArtist")
        verify(artist !== null)
        var albumText = ""
        var kids = find("vinylSongAlbum").children
        for (var i = 0; i < kids.length; i++) if (kids[i].text !== undefined) albumText = kids[i].text
        compare(albumText, "The Long Way", "book title is the album line")
        compare(artist.names.join(" "), "Becky Chambers")
        var narrators = find("audiobookNarrators")
        verify(narrators.visible)
        compare(narrators.text, "Narrated by Rachel Dulude")
    }

    function test_noNarratorsMeansNoNarratorLine() {
        makePage({ narrators: [] })
        verify(!visibleNamed("audiobookNarrators"))
    }

    function test_titleFallsBackToBookWhenNoChapters() {
        makePage({}, { chapterModel: [] })
        compare(find("vinylSongTitle").text, "The Long Way")
        verify(!visibleNamed("audiobookChapters"))
    }

    function test_playPauseTogglesStub() {
        makePage()
        mouseClick(find("vinylBarPlay"))
        compare(stub.callNames().join(), "togglePlay")
        compare(stub.paused, false)
        mouseClick(find("vinylBarPlay"))
        compare(stub.callNames().join(), "togglePlay,togglePlay")
    }

    function test_keyboardSeekCallsSeekTo() {
        makePage()
        var seek = find("vinylBarSeek")
        verify(seek.enabled)
        seek.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(stub.calls[0][0], "seekTo")
        compare(stub.calls[0][1], 70)
    }

    function test_pointerSeekCallsSeekTo() {
        makePage()
        var seek = find("vinylBarSeek")
        mouseClick(seek, seek.width / 2, seek.height / 2)
        compare(stub.calls[0][0], "seekTo")
        verify(Math.abs(stub.calls[0][1] - 300) < 3)
    }

    function test_nextAndPreviousJumpChaptersAndDisableAtEnds() {
        makePage()
        verify(!find("vinylBarPrev").enabled, "first chapter: no previous")
        verify(find("vinylBarNext").enabled)
        mouseClick(find("vinylBarNext"))
        compare(stub.calls[0][0], "goToChapter")
        compare(stub.calls[0][1], 1)
        verify(find("vinylBarPrev").enabled)
        mouseClick(find("vinylBarNext"))
        compare(stub.calls[1][1], 2)
        verify(!find("vinylBarNext").enabled, "last chapter: no next")
        mouseClick(find("vinylBarPrev"))
        compare(stub.calls[2][1], 1)
    }

    function test_chapterTapJumpsAndCurrentIsHighlighted() {
        makePage()
        var current = find("audiobookChapterHighlight_0").color
        var other = find("audiobookChapterHighlight_2").color
        verify(current.a > 0, "current chapter row is highlighted")
        compare(other.a, 0, "other rows are not")
        mouseClick(find("audiobookChapter_2"))
        compare(stub.calls[0][0], "goToChapter")
        compare(stub.calls[0][1], 2)
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
        compare(find("vinylSongTitle").text, "Part 1")
        verify(!find("audiobookChapterTime_0").visible, "no start time shown when it is zero")
        compare(find("audiobookChapterTime_1").text, "2:00")
        stub.position = 410
        tryCompare(find("vinylSongTitle"), "text", "Part 2")
    }

    function test_speedCycleFollowsTheLadder() {
        makePage()
        var expected = [1.25, 1.5, 1.75, 2.0, 0.75, 1.0]
        for (var i = 0; i < expected.length; i++) {
            mouseClick(find("audiobookSpeed"))
            compare(stub.speed, expected[i])
        }
        compare(find("audiobookSpeed").label, "1.0\u00d7")
        stub.speed = 1.5
        compare(find("audiobookSpeed").label, "1.5\u00d7")
    }

    function test_skipAndSleep() {
        makePage()
        mouseClick(find("audiobookSkipBack"))
        mouseClick(find("audiobookSkipForward"))
        compare(stub.calls[0][0], "seekRel")
        compare(stub.calls[0][1], -Data.SKIP_SECONDS)
        compare(stub.calls[1][1], Data.SKIP_SECONDS)
        compare(Data.SKIP_SECONDS, 10, "matches the reader's step")
        compare(find("audiobookSleep").label, "Sleep")
        mouseClick(find("audiobookSleep"))
        compare(stub.sleepMinutes, 15)
        compare(find("audiobookSleep").label, "15 min")
        for (var i = 0; i < 4; i++) mouseClick(find("audiobookSleep"))
        compare(stub.sleepMinutes, 0, "the ladder wraps back to off")
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

    // ---- the pure mapping ----
    function test_buildMapsSessionAndBook() {
        stub = createTemporaryObject(stubComponent, tc)
        stub.currentIndex = 1
        var pd = Data.build(stub, { title: "T", authors: ["A", "B"], narrators: ["N"], cover: "C:\\books\\t.jpg" })
        compare(pd.playingTrack.title, "Chapter One")
        compare(pd.playingEdition.artist, "A \u00b7 B")
        compare(pd.playingEdition.narrators[0], "N")
        compare(pd.artwork(pd.playingEdition.cover), "file:///C:/books/t.jpg")
        compare(pd.position, 65)
        compare(pd.canNext, true)
        compare(pd.canPrevious, true)
        pd.next(1)
        compare(stub.calls[0][1], 2)
        pd.next(-1)   // the stub moved to chapter 2 after the first jump
        compare(stub.calls[1][1], 1)
        pd.seek(5)
        compare(stub.calls[2][0], "seekTo")
        compare(pd.lyricsShown, false)
        compare(pd.videoShown, false)
        compare(pd.queueEnabled, false)
        compare(pd.geniusSongId, "")
        compare(Data.formatTime(3725), "1:02:05")
        compare(Data.formatTime(65), "1:05")
        compare(pd.artwork(""), "", "absent cover stays absent")
        compare(Data.build(stub, {}).playingEdition.artist, "", "absent authors stay absent")
    }

    function test_pageDataExposesEveryMappedField() {
        makePage()
        var snap = Data.build(stub, page.book)
        var missing = []
        for (var key in snap) {
            if (!(key in page.pageData)) missing.push(key)
        }
        compare(missing.join(), "", "page data lacks fields the mapping defines")
    }
}
