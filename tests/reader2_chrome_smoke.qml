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
import "../qml/reader2/Reader2Logic.js" as L

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

    // The SelectionMenu popover (Task 9) — bridge-free, driven by a sample rect. Proves it
    // parses + instantiates + the swatch/copy delegates realize, and that its signals fire.
    property string pickedColor: ""
    property int copyCount: 0
    property int dismissCount: 0
    property int defineCount: 0
    property int deleteCount: 0
    property string savedNote: ""
    R.SelectionMenu {
        id: selMenu
        width: 1280
        height: 720
        shown: true
        sel: ({ x: 600, y: 300, w: 140, h: 22 })
        onColorPicked: (c) => pickedColor = c
        onCopyRequested: copyCount++
        onDefineRequested: defineCount++
        onDeleteRequested: deleteCount++
        onNoteSaved: (n) => savedNote = n
        onDismissed: dismissCount++
    }

    // DictCard (Task 9 R2) — the Define glass card. Prove it instantiates in every state,
    // clamps in-frame, and fires its signals.
    property int dictDismissCount: 0
    property int openExternalCount: 0
    R.DictCard {
        id: dictCard
        width: 1280
        height: 720
        shown: true
        anchorRect: ({ x: 600, y: 300, w: 140, h: 22 })
        word: "whale"
        dictState: "ok"
        entries: L.dictParse(JSON.stringify({
            en: [{ partOfSpeech: "Noun", definitions: [{ definition: "A very large marine mammal." }] }]
        }))
        onDismissed: dictDismissCount++
        onOpenExternal: openExternalCount++
    }

    // FootnoteCard (Task 9 R2) — the footnote peek card.
    property int fnDismissCount: 0
    R.FootnoteCard {
        id: fnCard
        width: 1280
        height: 720
        shown: true
        anchorRect: ({ x: 300, y: 200, w: 12, h: 16 })
        text: "A note about the great fish of the sea, extracted plain."
        onDismissed: fnDismissCount++
    }

    // AppearancePanel (Task 10) — the right glass column. Bridge-free: it takes the current
    // appearance and reports each edit via changed(key, value). Prove it instantiates, derives
    // its current values from `appearance`, and its edit signal carries (key, value).
    property var sampleAppearance: L.appearanceDefaults()
    property string appEditKey: ""
    property var appEditValue: null
    property int appCloseCount: 0
    R.AppearancePanel {
        id: appPanel
        width: 1280
        height: 720
        open: true
        appearance: sampleAppearance
        onChanged: (key, value) => { appEditKey = key; appEditValue = value }
        onCloseRequested: appCloseCount++
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

            // ---- SelectionMenu (Task 9) instantiates, positions in-bounds, fires signals ----
            check(selMenu !== null && selMenu.visible === true, "SelectionMenu instantiated + visible when shown")
            check(selMenu.cardW > 0 && selMenu.cardH > 0, "SelectionMenu card has a positive size")
            check(selMenu.pos.x >= 0 && selMenu.pos.x <= selMenu.width - selMenu.cardW,
                  "SelectionMenu card clamped horizontally in-frame")
            check(selMenu.pos.y >= 0 && selMenu.pos.y <= selMenu.height - selMenu.cardH,
                  "SelectionMenu card clamped vertically in-frame")
            selMenu.colorPicked("#F0C24A"); check(pickedColor === "#F0C24A", "SelectionMenu colorPicked signal carries the color")
            selMenu.copyRequested();        check(copyCount === 1, "SelectionMenu copyRequested signal fires")
            selMenu.dismissed();            check(dismissCount === 1, "SelectionMenu dismissed signal fires")

            // ---- SelectionMenu Round-2: modes + new actions ----
            selMenu.shown = true
            selMenu.mode = "select"; check(selMenu.mode === "select", "SelectionMenu select mode")
            selMenu.defineRequested();  check(defineCount === 1, "SelectionMenu defineRequested fires")
            selMenu.noteSaved("a note"); check(savedNote === "a note", "SelectionMenu noteSaved carries the text")
            // note editor toggles the card size (taller when editing).
            var hSmall = selMenu.cardH
            selMenu.noteEditing = true;  check(selMenu.cardH > hSmall, "SelectionMenu grows for the note editor")
            selMenu.noteEditing = false
            // existing-highlight mode exposes Delete.
            selMenu.mode = "existing"; check(selMenu.mode === "existing", "SelectionMenu existing mode")
            selMenu.deleteRequested();  check(deleteCount === 1, "SelectionMenu deleteRequested fires")
            selMenu.shown = false;      check(selMenu.visible === false, "SelectionMenu hides when shown=false")

            // ---- DictCard (Task 9 R2) ----
            check(dictCard !== null && dictCard.visible === true, "DictCard instantiated + visible when shown")
            check(dictCard.cardW > 0 && dictCard.cardH > 0, "DictCard has a positive card size")
            check(dictCard.pos.x >= 0 && dictCard.pos.x <= dictCard.width - dictCard.cardW,
                  "DictCard clamped horizontally in-frame")
            check(dictCard.pos.y >= 0 && dictCard.pos.y <= dictCard.height - dictCard.cardH,
                  "DictCard clamped vertically in-frame")
            check(dictCard.entries.length === 1 && dictCard.entries[0].definitions.length === 1,
                  "DictCard entries bound from dictParse")
            dictCard.dictState = "loading"; check(dictCard.cardH > 0, "DictCard loading state sizes")
            dictCard.dictState = "empty";   check(dictCard.cardH > 0, "DictCard empty state sizes")
            dictCard.openExternal();        check(openExternalCount === 1, "DictCard openExternal fires")
            dictCard.dismissed();           check(dictDismissCount === 1, "DictCard dismissed fires")
            dictCard.shown = false;         check(dictCard.visible === false, "DictCard hides when shown=false")

            // ---- FootnoteCard (Task 9 R2) ----
            check(fnCard !== null && fnCard.visible === true, "FootnoteCard instantiated + visible when shown")
            check(fnCard.cardW > 0 && fnCard.cardH > 0, "FootnoteCard has a positive card size")
            check(fnCard.pos.x >= 0 && fnCard.pos.x <= fnCard.width - fnCard.cardW,
                  "FootnoteCard clamped horizontally in-frame")
            check(fnCard.pos.y >= 0 && fnCard.pos.y <= fnCard.height - fnCard.cardH,
                  "FootnoteCard clamped vertically in-frame")
            fnCard.dismissed();  check(fnDismissCount === 1, "FootnoteCard dismissed fires")
            fnCard.shown = false; check(fnCard.visible === false, "FootnoteCard hides when shown=false")

            // ---- AppearancePanel (Task 10) instantiates, derives values, fires its edit signal ----
            check(appPanel !== null && appPanel.open === true, "AppearancePanel instantiated + open")
            check(appPanel.curTheme === "night" && appPanel.curFont === "literata",
                  "AppearancePanel derives theme/font from appearance")
            check(appPanel.curSize === 18 && appPanel.curMargin === 72 && appPanel.curJustify === true,
                  "AppearancePanel derives size/margins/justify")
            check(appPanel.curRulerOn === false && appPanel.curBand === 92 && appPanel.curDim === 42,
                  "AppearancePanel derives ruler controls")
            // a different bound appearance flows through to the derived values.
            appPanel.appearance = L.mergeAppearance(L.appearanceDefaults(), { theme: "sepia", font: "inter", sizePx: 22 })
            check(appPanel.curTheme === "sepia" && appPanel.curFont === "inter" && appPanel.curSize === 22,
                  "AppearancePanel re-derives on a new appearance")
            // the changed(key,value) signal carries the edit (what ReaderShell persists + live-applies).
            appPanel.changed("theme", "slate")
            check(appEditKey === "theme" && String(appEditValue) === "slate", "AppearancePanel changed(key,value) carries the edit")
            appPanel.closeRequested()
            check(appCloseCount === 1, "AppearancePanel closeRequested fires")
            appPanel.open = false; check(appPanel.open === false, "AppearancePanel closes")

            // ---- ReaderChrome right-panel wiring (Task 10): toggle, mutual exclusivity, pin, Esc ----
            chrome.appearance = L.appearanceDefaults()
            chrome.handleAppearance()
            check(chrome.appearanceOpen === true && chrome.anyPanelOpen === true, "handleAppearance opens the right panel")
            check(chrome.awake === true && chrome.revealState.pinned === true, "open appearance PINS the chrome shown")
            chrome.tick(); check(chrome.awake === true, "pinned-by-appearance survives a tick")
            // mutual exclusivity: opening Contents closes Appearance.
            chrome.handleContents()
            check(chrome.panelOpen === true && chrome.appearanceOpen === false, "opening Contents closes Appearance (mutually exclusive)")
            // and opening Appearance closes Contents.
            chrome.handleAppearance()
            check(chrome.appearanceOpen === true && chrome.panelOpen === false, "opening Appearance closes Contents")
            // handleAppearance again toggles the right panel shut.
            chrome.handleAppearance()
            check(chrome.appearanceOpen === false, "handleAppearance again closes the right panel")
            check(chrome.revealState.pinned === false, "closing the last panel unpins the chrome")
            // closeAnyPanel drops whichever is open (the Esc target).
            chrome.handleAppearance(); check(chrome.appearanceOpen === true, "reopen appearance for closeAnyPanel")
            chrome.closeAnyPanel()
            check(chrome.panelOpen === false && chrome.appearanceOpen === false, "closeAnyPanel closes both panels")
            check(chrome.revealState.pinned === false, "closeAnyPanel unpins the chrome")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
