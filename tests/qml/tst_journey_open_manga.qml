// J1-Manga-Seam (visibility phase 2) — the reader's authoritative journey-observability seam,
// as real Qt Quick Test cases against the PRODUCTION ComicReaderShell.qml (the shared root behind
// both MangaReader.qml and VaultComicReader.qml, both via objectName "comicReaderShell" — the seam
// itself is documented inline on the properties it adds, ComicReaderShell.qml's "journey
// observability" section).
//
// This proves the SHELL's own state-derivation contract for readerReady/readerSourceId/
// readerPageCount/readerPageIndex given a simulated presented() call — exactly the same pattern
// tst_comicreader_resume_race.qml already uses (shell._onPresented(page, frac) stands in for what a
// real mounted surface reports once its Image(s) reach Image.Ready, or Strip settles its scroll
// position — see ComicReaderShell.qml's "journey observability" section for the full citation).
// Real Image-decode timing and the assembled-app proof belong to the LATER J1-Manga journey slice,
// which this seam only unblocks; this file proves the shell's own boolean logic in isolation.
import QtQuick
import QtQuick.Window 2.15
import QtTest 1.3

TestCase {
    id: testCase
    name: "JourneyOpenManga"

    // A REAL window (not just the invisible TestCase root — an item parented under an invisible
    // ancestor reports its OWN `visible` as false regardless of what is assigned to it, confirmed
    // live against this exact runner). The mandated negative control below assigns `reader.visible`
    // directly and needs that assignment to be observable, matching how a real caller's window shows
    // the reader.
    Window { id: testWindow; width: 640; height: 480; visible: true }

    component FakePageStore: QtObject {
        property var pages: []
        function localPages(cid) { return pages }
        function downloadChapter() {}
        function downloadIssue() {}
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
    }

    Component { id: storeComp; FakePageStore {} }
    property var shellComp: null

    function fivePages() {
        var out = []
        for (var i = 0; i < 5; i++) out.push({ index: i, url: "file:///f/p" + i + ".png", group: -1 })
        return out
    }

    function makeShell(cfg) {
        var full = Object.assign({
            "width": 640, "height": 480, "recordDebounceMs": 20,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "seriesId": "journey-open-manga"
        }, cfg)
        var inst = createTemporaryObject(shellComp, testWindow.contentItem, full)
        verify(inst !== null, "shell createTemporaryObject must succeed")
        return inst
    }

    function makeStore(pages) {
        var s = createTemporaryObject(storeComp, testCase)
        s.pages = pages || []
        return s
    }

    function initTestCase() {
        shellComp = Qt.createComponent("../../qml/comicreader/ComicReaderShell.qml")
        verify(shellComp.status !== Component.Error,
               "shell component must load: " + shellComp.errorString())
    }

    // No chapter has ever been opened: curChapterId is still "", so readerSourceId is empty and
    // readerReady must be false regardless of anything else. `visible: false` here is deliberately
    // pinned (a fresh, unopened reader is exactly this) so the negative control below (which
    // temporarily substitutes reader visibility for the real definition) reads the SAME false value
    // it would under the real one — this case must NOT flip under that substitution.
    function test_ready_false_before_source() {
        var shell = makeShell({ "pageStore": makeStore(fivePages()), "visible": false })
        compare(shell.readerSourceId, "", "no chapter opened yet: readerSourceId must be empty")
        compare(shell.readerReady, false, "readerReady must be false before any source is open")
    }

    // A chapter IS selected (a real caller would have the reader on screen — VaultComicReader/
    // MangaReader show it whenever a chapter id is chosen, whether or not it turns out downloaded),
    // but the page store answers empty: nothing is actually downloaded. readerPageCount must be 0
    // and readerReady must be false. `visible: true` is deliberately pinned here — a chapter IS
    // selected — so the negative control's substituted (visibility-only) binding reads TRUE while
    // the real one reads FALSE: this is the one case the mandated negative control must catch.
    function test_ready_false_before_page_model() {
        var shell = makeShell({ "chapterId": "ch1", "pageStore": makeStore([]), "visible": true })
        compare(shell.readerSourceId, "ch1", "a chapter is selected (test invalid otherwise)")
        compare(shell.readerPageCount, 0, "the store answered nothing: page model must read empty")
        compare(shell.readerReady, false, "readerReady must be false before the page model resolves")
    }

    // The real false -> true transition (Completion signal: "the Quick Test observes the real
    // false->true transition"). shellA proves the BEFORE moment stays false; shellB proves the AFTER
    // moment (one real _onPresented() call — the exact call a mounted surface makes once its
    // Image(s) reach Image.Ready (Single/Double) or Strip settles its scroll position, see
    // ComicReaderShell.qml's "journey observability" section) goes true. `visible` is deliberately
    // false on both instances until the exact moment each expects "shown": never for shellA (it must
    // stay unready throughout), and only right before shellB's final assertion for shellB (mirroring
    // a real caller only showing the reader once ready) — this keeps the mandated negative control
    // (which substitutes reader visibility for the real definition) reading the SAME value the real
    // definition would at every assertion in this test, so only the page-model case may flip red.
    function test_ready_true_after_first_page() {
        var shellA = makeShell({ "chapterId": "ch1", "pageStore": makeStore(fivePages()),
                                  "visible": false })
        compare(shellA.readerPageCount, 5, "page model must be resolved (test invalid otherwise)")
        compare(shellA.readerReady, false,
                "readerReady must stay false after load() alone — before any surface has genuinely " +
                "presented a page. This is the exact race the seam closes: load() seeds " +
                "presentedPage = currentPage synchronously as the progress-record anchor, which must " +
                "NOT by itself count as render-ready.")
        shellA.destroy()

        var shellB = makeShell({ "chapterId": "ch1", "pageStore": makeStore(fivePages()),
                                  "visible": false })
        shellB.setLayout("single_page")   // isolate from strip-restore timing, like the resume-race gate
        compare(shellB.readerReady, false, "still false before the surface reports (fresh instance)")
        shellB._onPresented(1, 0)          // the ONE real surface signal this seam is bound to
        shellB.visible = true              // now shown, matching a real caller revealing a ready reader
        compare(shellB.readerReady, true,
                "readerReady must be true once the current page is genuinely presented")
    }

    // readerSourceId / readerPageCount / readerPageIndex must track the authoritative model exactly,
    // including across a real page navigation — not merely match once at construction.
    function test_source_count_index_match_model() {
        var shell = makeShell({ "chapterId": "ch1", "pageStore": makeStore(fivePages()) })
        shell.setLayout("single_page")
        compare(shell.readerSourceId, shell.curChapterId, "readerSourceId must mirror curChapterId")
        compare(shell.readerPageCount, shell.pageCount, "readerPageCount must mirror pageCount")
        compare(shell.readerPageIndex, shell.currentPage - 1,
                "readerPageIndex must be the 0-based mirror of currentPage")
        compare(shell.readerPageIndex, 0, "opens on page 1 -> index 0")

        shell.goToPageIndex(3)   // 1-based page number, despite the name — see ComicReaderShell.qml
        compare(shell.currentPage, 3, "navigation must have actually moved (test invalid otherwise)")
        compare(shell.readerPageIndex, shell.currentPage - 1,
                "readerPageIndex must keep tracking currentPage after navigation")
        compare(shell.readerPageIndex, 2, "page 3 -> index 2")
        compare(shell.readerSourceId, shell.curChapterId, "readerSourceId must still mirror curChapterId")
        compare(shell.readerPageCount, shell.pageCount, "readerPageCount must still mirror pageCount")
    }
}
