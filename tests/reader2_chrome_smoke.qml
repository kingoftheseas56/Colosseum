// reader2_chrome_smoke — headless proof that the chrome QML tree (TASK 7 + the TASK 8
// LEFT PANEL) parses and instantiates CLEAN, with NO WebEngine and NO bridge (the
// overlay is bridge-free). A parse error, a float font.pixelSize, an unresolved Theme
// token, or a delegate that throws on the pure row-shapers would fail the engine load
// (qml.exe exits non-zero) or a check below. Run:
//   qml.exe -platform offscreen tests/reader2_chrome_smoke.qml
// Verdict via console + Qt.exit. Body wrapped in try/catch (throw HANGS offscreen).
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "../qml/reader2" as R

Item {
    width: 1280
    height: 720

    // sample data for the left panel (drives every pane's delegates + row-shapers).
    readonly property var sampleToc: [
        { index: 0, label: "Etymology", href: "a.html" },
        { index: 1, label: "Loomings", href: "b.html", fraction: 0.12 },
        { index: 2, label: "The Carpet-Bag", href: "c.html", fraction: 0.2 }
    ]
    readonly property var sampleBookmarks: [
        { id: "b1", locator: { cfi: "epubcfi(/6/4)" }, label: "Loomings", snippet: "Page 4 of 18", page: 4 },
        { id: "b2", locator: { cfi: "epubcfi(/6/6)" }, label: "Ch 1 · 41%", snippet: "Ch 1 · 41%" }
    ]
    readonly property var sampleHighlights: [
        { id: "a1", cfi: "epubcfi(/6/8)", text: "Call me Ishmael.", color: "#FEF3BD", note: "famous line", chapterLabel: "Loomings" },
        { id: "a2", value: "epubcfi(/6/9)", text: "This is my substitute for pistol and ball.", color: "#B5E0C6", chapterLabel: "Loomings" }
    ]

    R.ReaderChrome {
        id: chrome
        anchors.fill: parent
        title: "Moby-Dick"
        author: "Herman Melville"
        chapterLabel: "Loomings"
        percent: 2
        pageInChapter: 4
        pagesInChapter: 18
        ticks: [0.1, 0.3, 0.6, 0.9]
        returnVisible: true
        returnPageLabel: "4"
        tocModel: sampleToc
        currentTocIndex: 1
        bookmarks: sampleBookmarks
        highlights: sampleHighlights
    }

    // A DIRECT LeftPanel instance, sized + open, so every pane's ListView actually
    // realizes its delegates (which call the pure row-shapers) under -platform offscreen.
    R.LeftPanel {
        id: panel
        width: 400
        height: 600
        open: true
        activeTab: "contents"
        tocModel: sampleToc
        currentTocIndex: 1
        bookmarks: sampleBookmarks
        highlights: sampleHighlights
    }

    Component.onCompleted: {
        var fails = 0
        function check(ok, what) { if (!ok) { console.log("FAIL " + what); fails++ } else console.log("ok   " + what) }
        try {
            // ---- Theme + chrome (Task 7 baseline, still green) ----
            check(String(R.Theme.gold).toLowerCase() === "#f0c24a", "Theme singleton resolves (gold)")
            check(chrome !== null, "ReaderChrome instantiated (TopBar + BottomRail + LeftPanel children)")
            check(chrome.awake === false, "chrome starts asleep (naked reading surface)")

            // ---- LeftPanel instantiates + binds (Task 8) ----
            check(panel !== null, "LeftPanel instantiated")
            check(panel.open === true, "LeftPanel open property binds")
            // cycle every tab so each pane's delegates build without warnings.
            panel.activeTab = "contents";   check(panel.activeTab === "contents", "LeftPanel tab -> contents")
            panel.activeTab = "bookmarks";  check(panel.activeTab === "bookmarks", "LeftPanel tab -> bookmarks")
            panel.activeTab = "highlights"; check(panel.activeTab === "highlights", "LeftPanel tab -> highlights")
            // empty arrays must render the placeholders, not crash.
            panel.bookmarks = []; panel.highlights = []; panel.tocModel = []
            check(panel.tocModel.length === 0, "LeftPanel empty toc -> placeholder path (no crash)")
            panel.tocModel = sampleToc
            panel.open = false; check(panel.open === false, "LeftPanel closes")

            // ---- panel wiring on the chrome (Contents icon toggle + pin) ----
            chrome.handleContents()
            check(chrome.panelOpen === true && chrome.activeTab === "contents", "handleContents opens to Contents")
            check(chrome.awake === true && chrome.revealState.pinned === true, "open panel PINS the chrome shown")
            chrome.tick()   // a tick while the panel pins must NOT hide the chrome
            check(chrome.awake === true, "pinned-by-panel survives a tick")
            chrome.handleContents()
            check(chrome.panelOpen === false, "handleContents again closes the panel")
            check(chrome.revealState.pinned === false, "closing the panel unpins the chrome")
            chrome.openPanelTo("bookmarks")
            check(chrome.panelOpen === true && chrome.activeTab === "bookmarks", "openPanelTo switches tab + opens")
            chrome.closePanel()
            check(chrome.panelOpen === false, "closePanel closes")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
