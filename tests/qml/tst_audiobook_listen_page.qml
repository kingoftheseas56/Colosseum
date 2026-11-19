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
        compare(find("audiobookIdentity").text, "Becky Chambers · The Long Way")
        verify(find("audiobookNarrators").visible)
        compare(find("audiobookNarrators").text, "Narrated by Rachel Dulude")
    }

    function test_backdropAndPoster() {
        var cover = Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg")
        makePage({id: "42", cover: String(cover)})
        var backdrop = find("vinylBackdrop")
        verify(backdrop.visible)
        compare(String(backdrop.source), String(cover))
        compare(backdrop.width, page.width)
        compare(backdrop.height, page.height)
        var poster = find("audiobookPoster")
        verify(poster !== null)
        compare(poster.width, 290)
        compare(poster.height, 290)
        compare(poster.x, 495)
        compare(poster.y, 138)
        var collections = find("audiobookAddToCollections")
        verify(collections.visible)
        compare(collections.reference.sourceId, "42")
        compare(collections.reference.editionId, "audiobook")
    }

    function test_readAlongAvailabilityAndSignal() {
        makePage()
        var button = find("audiobookReadAlong")
        verify(button !== null && button.visible)
        verify(!button.enabled)
        compare(button.opacity, 0.34)
        var fired = 0
        page.readAlongRequested.connect(function () { fired++ })
        mouseClick(button)
        compare(fired, 0)
        page.ebookAvailable = true
        verify(button.enabled)
        compare(button.opacity, 1)
        mouseClick(button)
        compare(fired, 1)
    }

    function test_playerBarOwnsPlaybackAndSaveStillWorks() {
        makePage({}, {ready: false})
        var playButton = find("abBarPlay")
        compare(find("audiobookPrimary"), null)
        verify(!playButton.enabled)
        stub.ready = true
        verify(playButton.enabled)

        var saves = 0
        page.saveToggled.connect(function () { saves++ })
        mouseClick(find("audiobookSave"))
        compare(saves, 1)

        var identity = find("audiobookIdentity")
        var titleWithSave = identity.mapToItem(page, 0, 0).y
        page.saveAvailable = false
        verify(!visibleNamed("audiobookSave"))
        verify(identity.mapToItem(page, 0, 0).y < titleWithSave)
    }

    function test_noNarratorsMeansNoNarratorLine() {
        makePage({ narrators: [] })
        verify(!visibleNamed("audiobookNarrators"))
    }

    function test_bookTitleRemainsVisibleWithoutChapters() {
        makePage({}, { chapterModel: [] })
        compare(find("audiobookIdentity").text, "Becky Chambers · The Long Way")
        mouseClick(find("audiobookChaptersButton"))
        verify(!visibleNamed("audiobookChapters"))
    }

    function test_wideAudiobookBarHasNoChaptersButton() {
        makePage()
        var bar = find("audiobookListenBar")
        verify(bar !== null && bar.wide)
        compare(bar.width, 1172)
        compare(bar.height, 104)
        verify(visibleNamed("abBarSpeed"))
        verify(!visibleNamed("abBarChapters"))
        compare(find("vinylPlayerBar"), null)
    }

    function test_wholeBookLineIsVisible() {
        makePage()
        verify(visibleNamed("abBarBookLine"))
    }

    function test_chaptersButtonOpensChapterList() {
        makePage()
        verify(!visibleNamed("audiobookChapters"))
        var button = find("audiobookChaptersButton")
        verify(button !== null)
        mouseClick(button)
        verify(visibleNamed("audiobookChapterList"))
        var panel = find("audiobookChapters")
        var poster = find("audiobookPoster")
        verify(panel.x + panel.width <= poster.x)
        mouseClick(button)
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
        mouseClick(find("audiobookChaptersButton"))
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
        mouseClick(find("audiobookChaptersButton"))
        compare(find("audiobookIdentity").text, "Becky Chambers · The Long Way")
        compare(find("abBarChapterLabel").text, "Part 1")
        verify(visibleNamed("audiobookChapterMarker_1"))
        verify(!visibleNamed("audiobookChapterMarker_0"))
        compare(find("audiobookChapter_2").accessibleDescription, "Starts at 6:40")
        stub.position = 410
        tryCompare(find("abBarChapterLabel"), "text", "Part 2")
        verify(visibleNamed("audiobookChapterMarker_2"))
        verify(!visibleNamed("audiobookChapterMarker_1"))
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

    function test_noVinylOnlyControlIsVisible() {
        makePage({ description: "A crew crosses the galaxy.", seriesLine: "Wayfarers, book 1" })
        verify(!visibleNamed("audiobookNotes"))
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
        verify(!visibleNamed("vinylSongAlbum"))
    }

    function test_backRequested() {
        makePage()
        var fired = 0
        page.backRequested.connect(function () { fired++ })
        mouseClick(find("audiobookBack"))
        compare(fired, 1)
    }

    function test_bookMetadataAndChapterTimeMapping() {
        makePage({ title: "T", authors: ["A", "B"], narrators: ["N"] })
        var edition = Data.edition(page.book)
        compare(edition.title, "T")
        compare(edition.artist, "A · B")
        compare(edition.narrators[0], "N")
        compare(Data.coverUrl("C:\\books\\t.jpg"), "file:///C:/books/t.jpg")
        compare(Data.coverUrl(""), "")
        compare(Data.formatTime(3725), "1:02:05")
        compare(Data.formatTime(65), "1:05")
        compare(Data.edition({}).artist, "")
    }
}
