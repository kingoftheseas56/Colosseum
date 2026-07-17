// ReaderShell.qml — the reader component Biblio embeds on swap day (Task 16).
//
// Composition: the web Paper on the bottom, the native ReaderChrome (glass over
// paper — TASK 7) on top. The chrome stays hidden while you read and returns only when
// you reach for the top/bottom edge (or double-click / the book-open orientation beat),
// turns pages at the edges, and scrubs the gold rail; ReaderShell owns the wiring to
// the paper + the native stores. Keyboard turns (Right/Space/PageDown → next,
// Left/PageUp → prev, Esc → back) are handled IN-PAGE by the glue — the web view owns
// focus + keyboard (old-reader model) — and arrive here as semantic paper events
// ('escape', 'selectionCleared'); they NEVER wake the chrome (the naked surface's point).
//
// The RESUME SEAM (Task 6) is unchanged: every 'relocated' persists position to the
// SAME progress.json the old reader uses, and reopening returns to where you left off.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

FocusScope {
    id: shell
    property string bookPath: ""
    // Store key = the SHA1[:20] fingerprint of the path, NOT the raw path. The old
    // reader keyed progress/bookmarks/annotations by this (BookBridge::progressKey);
    // deriving it here is what makes positions/marks survive the swap (zero migration).
    // Reader2Bridge.bookKey mirrors that derivation byte-for-byte (both delegate to
    // BookStores::keyFor, the single shared formula).
    property string bookId: bookPath === "" ? "" : Reader2Bridge.bookKey(bookPath)
    signal closed()
    focus: true

    // ---- chrome view-model (fed by paper 'ready' + 'relocated' events) ----
    property string bookTitle: ""
    property string bookAuthor: ""
    property string chapterLabel: ""
    property int percent: 0
    property int pageInChapter: 0
    property int pagesInChapter: 0
    property var chapterTicks: []
    // remembered position for the "Return to page N" ghost chip after a scrub/jump.
    property string lastCfi: ""
    property int lastPageInChapter: 0
    property string rememberedCfi: ""
    property bool returnVisible: false
    property string returnPageLabel: ""

    // ---- left-panel view-model (Task 8) ----
    // The raw toc array from 'ready' (each entry {index,label,href,fraction?}); the panel
    // renders it and the rail derives ticks from it. currentTocIndex tracks relocated so
    // the Contents pane can dim read rows + gold the current one. bookmarks/highlights are
    // loaded from the SHARED stores through Reader2Bridge (zero migration).
    property var bookToc: []
    property int currentTocIndex: -1
    property real lastFraction: 0
    property var bookmarks: []
    property var highlights: []

    // ---- selection / highlight view-model (Task 9, the pen) ----
    // Stashed from the paper's 'selection' / 'highlightTapped' event; drive the native
    // SelectionMenu popover. selMenuMode = "select" (a fresh selection: color/note/define/copy)
    // or "existing" (a tapped highlight: delete + re-color); existingHlId is the tapped
    // highlight's id in "existing" mode.
    property var selRect: ({ x: 0, y: 0, w: 0, h: 0 })
    property string selText: ""
    property string selCfi: ""
    property bool selMenuShown: false
    property string selMenuMode: "select"
    property string existingHlId: ""

    // ---- Define (dictionary) card view-model (Round 2) ----
    property bool dictShown: false
    property var dictRect: ({ x: 0, y: 0, w: 0, h: 0 })
    property string dictWord: ""
    property var dictEntries: []
    property string dictState: "loading"   // "loading" | "ok" | "empty"

    // ---- footnote card view-model (Round 2) ----
    property bool footnoteShown: false
    property var footnoteRect: ({ x: 0, y: 0, w: 0, h: 0 })
    property string footnoteText: ""
    // Keep the web view focused while the popover is up. QtWebEngine only DRAWS the selection
    // while the view has focus — on blur it hides the visual selection but keeps the DOM
    // selection (so no selectionchange fires, and the menu would float over an invisible
    // selection: the exact "selection gone but pop-up isn't" bug). Re-assert focus when the
    // menu opens; callLater beats any async focus-steal from the overlay arming.
    onSelMenuShownChanged: if (shell.selMenuShown) Qt.callLater(function () { paper.focusPaper() })

    // Reload marks from the shared stores (on ready, on panel open, and after any change).
    function refreshMarks() {
        if (shell.bookPath === "") return
        var id = shell.bookId
        shell.bookmarks = Reader2Bridge.bookmarksGet(id)
        shell.highlights = Reader2Bridge.annotationsGet(id)
    }

    // Re-paint every stored highlight onto the paper (called after 'ready', once the
    // shared annotations are loaded). The glue's addHighlight is idempotent by id, so a
    // section reload / a repeat call never doubles a highlight. L.highlightRow normalizes
    // both reader2's write shape and the old annotations.json into { id, cfi, color }.
    function reapplyHighlights() {
        var list = shell.highlights || []
        for (var i = 0; i < list.length; i++) {
            var r = L.highlightRow(list[i])
            if (r.id !== "" && r.cfi !== "")
                paper.addHighlight({ id: r.id, cfi: r.cfi, color: r.color })
        }
    }

    // Dismiss the selection popover and drop the paper's live selection.
    function dismissSelectionMenu() {
        shell.selMenuShown = false
        shell.selMenuMode = "select"
        shell.existingHlId = ""
        paper.clearSelection()
    }

    // Persist a highlight for the CURRENT selection in `color`, with an optional `note`, and
    // paint it on the paper. The record shape satisfies BOTH readers: LeftPanel.highlightRow
    // reads cfi from value|cfi|locator.cfi, plus color/text/note/chapterLabel; the glue's
    // addHighlight needs { id, cfi, color }. annotationsSave (→ BookStores::listSave) STAMPS a
    // fresh id when absent AND returns the stored record, so we don't double-stamp — we read
    // the id back and hand that SAME id to the paper so re-apply/remove stay consistent across
    // a reopen. The Highlights pane renders `note` indented under the quote.
    function saveSelectionAnnotation(color, note) {
        if (shell.bookPath === "" || shell.selCfi === "") { shell.dismissSelectionMenu(); return }
        var rec = {
            cfi: shell.selCfi,          // read by highlightRow (and by us for addHighlight)
            value: shell.selCfi,        // the glue's annotation identity field (belt + suspenders)
            color: color,
            text: shell.selText,
            note: note,
            chapterLabel: shell.chapterLabel
        }
        var saved = Reader2Bridge.annotationsSave(shell.bookId, rec)   // stamps id, returns record
        var id = (saved && saved.id !== undefined && saved.id !== null) ? String(saved.id) : ""
        if (id !== "") paper.addHighlight({ id: id, cfi: shell.selCfi, color: color })
        shell.refreshMarks()                                          // Highlights pane picks it up
        shell.dismissSelectionMenu()
    }
    // A color dot on a fresh selection → highlight in that color (no note).
    function applyHighlight(color) { shell.saveSelectionAnnotation(color, "") }
    // The Note editor's Save → a gold highlight carrying the note. (Picking a color closes the
    // menu, so Note always defaults to gold; the note is what the user came for.)
    function applyNote(note) { shell.saveSelectionAnnotation("#F0C24A", note) }

    // ---- existing-highlight actions (SelectionMenu mode "existing") ----
    // Re-color a tapped highlight: update its stored color in place (listSave replaces by id),
    // then repaint (remove + re-add so the overlayer re-draws in the new color).
    function recolorHighlight(color) {
        if (shell.existingHlId === "") { shell.dismissSelectionMenu(); return }
        var list = shell.highlights || []
        var rec = null
        for (var i = 0; i < list.length; i++) {
            if (String(L.highlightRow(list[i]).id) === shell.existingHlId) { rec = list[i]; break }
        }
        if (!rec) { shell.dismissSelectionMenu(); return }
        var cfi = L.highlightRow(rec).cfi
        var updated = {}
        for (var k in rec) updated[k] = rec[k]     // preserve every field
        updated.color = color
        Reader2Bridge.annotationsSave(shell.bookId, updated)          // same id → in-place update
        if (cfi !== "") {
            paper.removeHighlight(shell.existingHlId)
            paper.addHighlight({ id: shell.existingHlId, cfi: cfi, color: color })
        }
        shell.refreshMarks()
        shell.dismissSelectionMenu()
    }
    // Delete a tapped highlight from the store AND the paper. The empty-id guard matters:
    // BookStores::listDelete treats an empty id as "clear ALL for this book", so we only ever
    // call it with a real id.
    function deleteHighlight() {
        if (shell.existingHlId !== "") {
            Reader2Bridge.annotationsDelete(shell.bookId, shell.existingHlId)
            paper.removeHighlight(shell.existingHlId)
            shell.refreshMarks()
        }
        shell.dismissSelectionMenu()
    }

    // ---- Define (dictionary) ----
    // Extract the first word of the selection and look it up (Wiktionary REST, C++ side). The
    // card opens in the loading state anchored at the selection; dictResult (below) fills it.
    function openDict() {
        var word = L.firstWord(shell.selText)
        if (word === "") { shell.dismissSelectionMenu(); return }
        shell.dictWord = word
        shell.dictEntries = []
        shell.dictState = "loading"
        shell.dictRect = shell.selRect
        shell.dictShown = true
        shell.dismissSelectionMenu()          // the selection menu gives way to the card
        Reader2Bridge.dictLookup(word)
    }
    function dismissDict() { shell.dictShown = false }
    function dismissFootnote() { shell.footnoteShown = false }

    // Ratified default paper appearance (Night) — pushed at open so the FIRST paint
    // already matches the mock. The full adjustable panel is Task 10; this is just the
    // correct default. (Shape mirrors paper_glue.js: theme{bg,fg}/font/sizePx/lineHeight/
    // marginPx/justify.)
    readonly property var defaultAppearance: ({
        theme: { bg: "#111013", fg: "#eee9de" },
        font: "book", sizePx: 18, lineHeight: 1.6, marginPx: 72, justify: true
    })

    // Keyboard now lives IN-PAGE (paper_glue.js): the web view owns focus + keys, so a key
    // turns the page from inside the paper and never fights QML focus. Page-turn keys arrive
    // as nothing to route here (the glue calls the renderer directly); Esc and an
    // arrow-key-cleared selection arrive as the 'escape' / 'selectionCleared' paper events
    // handled in onPaperEvent below. (The old QML Keys.onPressed page-turn + selection-guard
    // handler was removed with this move.)

    // Refresh marks whenever the panel opens, so a change made elsewhere shows up.
    Connections {
        target: chrome
        function onPanelOpenChanged() { if (chrome.panelOpen) shell.refreshMarks() }
    }

    Paper {
        id: paper
        anchors.fill: parent
        onGlueUpChanged: if (glueUp && shell.bookPath !== "") shell.openAtResume(shell.bookPath)
        onPaperEvent: (name, p) => {
            console.log("[shell]", name, JSON.stringify(p).slice(0, 160))

            if (name === "ready") {
                // book identity + toc arrive here (before the first relocate).
                shell.bookTitle = (p.metadata && p.metadata.title) ? String(p.metadata.title) : ""
                shell.bookAuthor = L.authorText(p.metadata)
                shell.bookToc = (p.toc && Array.isArray(p.toc)) ? p.toc : []
                shell.currentTocIndex = -1                        // unknown until the first relocate
                shell.chapterTicks = L.railTicks(p.toc, (p.toc && p.toc.length) ? p.toc.length : 0)
                shell.refreshMarks()                              // load bookmarks/highlights from the shared stores
                shell.reapplyHighlights()                         // re-paint stored highlights onto the fresh paper
                paper.setAppearance(shell.defaultAppearance)      // ratified Night default
                chrome.wake()                                     // orientation beat: show briefly on open, recede after 3s idle
                paper.focusPaper()                                // the web view owns keys — focus it so keys work immediately
            } else if (name === "toggleChrome") {
                // double-click on EMPTY paper space (glue) → toggle the chrome reveal.
                chrome.toggle()
            } else if (name === "escape") {
                // Esc from the glue's in-page keyboard. Cascading close, same order the old
                // reader uses: the pen's floating cards first (dict/footnote), then the
                // selection popover, then the left panel, then the book.
                if (shell.dictShown) shell.dismissDict()
                else if (shell.footnoteShown) shell.dismissFootnote()
                else if (shell.selMenuShown) shell.dismissSelectionMenu()
                else if (chrome.panelOpen) chrome.closePanel()
                else shell.closed()
            } else if (name === "footnote") {
                // A footnote/endnote link was tapped (the glue extracted its text). Show the
                // FootnoteCard near the anchor; the page does NOT navigate to the note.
                shell.footnoteText = (p.html !== undefined && p.html !== null) ? String(p.html) : ""
                shell.footnoteRect = (p.rect && typeof p.rect === "object") ? p.rect : ({ x: 0, y: 0, w: 0, h: 0 })
                if (shell.footnoteText !== "") shell.footnoteShown = true
                else console.log("[shell] footnote had no extractable text — skipping card")
            } else if (name === "highlightTapped") {
                // An existing highlight was tapped (the glue guards against a selection-ending
                // click also firing this). Open the SelectionMenu in "existing" mode at its
                // rect: Delete (+ re-color dots). p.id is the annotation id; p.rect the anchor.
                var hid = (p.id !== undefined && p.id !== null) ? String(p.id) : ""
                if (hid !== "") {
                    shell.existingHlId = hid
                    shell.selText = ""
                    shell.selCfi = ""
                    shell.selRect = (p.rect && typeof p.rect === "object") ? p.rect : ({ x: 0, y: 0, w: 0, h: 0 })
                    shell.selMenuMode = "existing"
                    shell.selMenuShown = true
                }
            } else if (name === "selectionCleared") {
                // The underlying selection went away (clicked elsewhere, an arrow key turned
                // the page out from under the menu). Dismiss the popover so it never floats
                // over a stale/empty selection.
                if (shell.selMenuShown) shell.dismissSelectionMenu()
            } else if (name === "selection") {
                // text selected in the paper → stash it + open the SelectionMenu at its rect.
                // A new selection while the menu is up simply re-stashes (the menu repositions).
                shell.selText = (p.text !== undefined && p.text !== null) ? String(p.text) : ""
                shell.selCfi = (p.cfi !== undefined && p.cfi !== null) ? String(p.cfi) : ""
                shell.selRect = (p.rect && typeof p.rect === "object") ? p.rect : ({ x: 0, y: 0, w: 0, h: 0 })
                shell.selMenuMode = "select"
                shell.existingHlId = ""
                shell.selMenuShown = (shell.selText !== "")
            } else if (name === "relocated" && shell.bookPath !== "") {
                // --- chrome view-model (rail + top bar + Contents current row) ---
                if (p.cfi !== undefined && p.cfi !== null) shell.lastCfi = String(p.cfi)
                if (p.chapterTitle !== undefined) shell.chapterLabel = String(p.chapterTitle)
                if (Number.isFinite(p.percent)) shell.percent = p.percent
                if (Number.isFinite(p.tocIndex)) shell.currentTocIndex = p.tocIndex
                if (Number.isFinite(p.fraction)) shell.lastFraction = p.fraction
                if (Number.isFinite(p.pageInChapter)) { shell.pageInChapter = p.pageInChapter; shell.lastPageInChapter = p.pageInChapter }
                if (Number.isFinite(p.pagesInChapter)) shell.pagesInChapter = p.pagesInChapter

                // --- RESUME SEAM save (Task 6, unchanged) ---
                // Read the key + prev entry AT SAVE TIME (shell.bookId, not a stale
                // capture) so a relocated can only ever write the CURRENT book. The
                // .pragma logic can't touch Date, so we stamp updatedAt here.
                var id = shell.bookId
                var prev = Reader2Bridge.progressGet(id)
                p.updatedAt = Date.now()
                Reader2Bridge.progressSave(id, L.progressRecord(prev, p, shell.bookPath))
            }
        }
    }

    ReaderChrome {
        id: chrome
        anchors.fill: parent
        title: shell.bookTitle
        author: shell.bookAuthor
        chapterLabel: shell.chapterLabel
        percent: shell.percent
        pageInChapter: shell.pageInChapter
        pagesInChapter: shell.pagesInChapter
        ticks: shell.chapterTicks
        returnVisible: shell.returnVisible
        returnPageLabel: shell.returnPageLabel

        // left-panel data (Task 8)
        tocModel: shell.bookToc
        currentTocIndex: shell.currentTocIndex
        bookmarks: shell.bookmarks
        highlights: shell.highlights

        onBackRequested: shell.closed()
        onPrevRequested: paper.prev()
        onNextRequested: paper.next()
        onScrubbed: (f) => {
            // remember where we jumped FROM so the ghost chip can bring us back.
            shell.rememberedCfi = shell.lastCfi
            shell.returnPageLabel = String(shell.lastPageInChapter)
            shell.returnVisible = true
            paper.goTo(f)                                 // number → goToFraction
        }
        onReturnRequested: {
            if (shell.rememberedCfi !== "") paper.goTo(shell.rememberedCfi)   // cfi → goTo
            shell.returnVisible = false
        }
        // --- left panel (Task 8) ---
        // TOC / bookmark / highlight jumps go straight to the paper; the panel STAYS open.
        onTocActivated: (href) => paper.goTo(href)
        onBookmarkActivated: (cfi) => { if (cfi !== "") paper.goTo(cfi) }
        onHighlightActivated: (cfi) => { if (cfi !== "") paper.goTo(cfi) }
        onBookmarkDeleted: (id) => {
            // GUARD: an empty itemId means "CLEAR ALL bookmarks for this book" to
            // BookStores::listDelete — a malformed id-less record must never wipe the whole
            // set. Only delete a real id (siblings guard cfi !== "" the same way).
            if (id !== "") {
                Reader2Bridge.bookmarksDelete(shell.bookId, id)
                shell.refreshMarks()
            }
        }
        onTabSelected: (tab) => shell.refreshMarks()

        // The bookmark icon = "bookmark THIS page" (per the mock). Write the SAME shape the
        // old reader's reader_bookmarks.js uses (locator{cfi,href,fraction} + label + snippet)
        // so marks survive the swap. No text snippet yet — that needs a paper round-trip to
        // pull the page's opening words; we use a "Page N of M" detail line for now (Task 9
        // area can enrich it). href comes from the current toc entry when known.
        onBookmarkRequested: {
            if (shell.bookPath === "" || shell.lastCfi === "") return
            var href = (shell.bookToc && shell.currentTocIndex >= 0 && shell.bookToc[shell.currentTocIndex])
                       ? String(shell.bookToc[shell.currentTocIndex].href || "") : ""
            var detail = shell.pagesInChapter > 0
                       ? ("Page " + shell.pageInChapter + " of " + shell.pagesInChapter) : ""
            var rec = {
                locator: { cfi: shell.lastCfi, href: href, fraction: shell.lastFraction },
                label: shell.chapterLabel,
                snippet: detail,
                page: shell.pageInChapter
            }
            Reader2Bridge.bookmarksSave(shell.bookId, rec)
            shell.refreshMarks()
        }

        // Panels for search / appearance arrive in Tasks 10-11; wired now, filled later.
        onSearchRequested: console.log("[shell] searchRequested (search sheet = Task 11)")
        onAppearanceRequested: console.log("[shell] appearanceRequested (appearance panel = Task 10)")
    }

    // The selection popover (Task 9, the pen). Declared near-LAST so it floats above the
    // chrome; bridge-free, driven by shell.sel* and reporting back via signals. Its own
    // backdrop dismisses on tap-outside; ReaderShell does the save/paint/copy/delete.
    SelectionMenu {
        id: selectionMenu
        anchors.fill: parent
        shown: shell.selMenuShown
        sel: shell.selRect
        mode: shell.selMenuMode
        // a color dot: highlight-in-color on a fresh selection, or re-color an existing one.
        onColorPicked: (color) => shell.selMenuMode === "existing"
                       ? shell.recolorHighlight(color) : shell.applyHighlight(color)
        onNoteSaved: (note) => shell.applyNote(note)
        onDefineRequested: shell.openDict()
        onDeleteRequested: shell.deleteHighlight()
        onCopyRequested: {
            if (shell.selText !== "") Clipboard.copy(shell.selText)   // exposed by main.cpp + the harness
            shell.dismissSelectionMenu()
        }
        onDismissed: shell.dismissSelectionMenu()
    }

    // The Define (dictionary) card. dictLookup is C++-side; dictResult (below) parses the
    // Wiktionary JSON into entries and flips the card to "ok"/"empty".
    DictCard {
        id: dictCard
        anchors.fill: parent
        shown: shell.dictShown
        anchorRect: shell.dictRect
        word: shell.dictWord
        entries: shell.dictEntries
        dictState: shell.dictState
        onDismissed: shell.dismissDict()
        onOpenExternal: {
            Qt.openUrlExternally("https://en.wiktionary.org/wiki/" + encodeURIComponent(shell.dictWord))
            shell.dismissDict()
        }
    }

    // The footnote/endnote peek card — text extracted by the glue, shown near the tap.
    FootnoteCard {
        id: footnoteCard
        anchors.fill: parent
        shown: shell.footnoteShown
        anchorRect: shell.footnoteRect
        text: shell.footnoteText
        onDismissed: shell.dismissFootnote()
    }

    // Dictionary result from the native seam (Wiktionary REST). Parse the JSON to entries;
    // empty / not-ok → the card's quiet "no definition" state (with Open-in-Wiktionary).
    Connections {
        target: Reader2Bridge
        function onDictResult(word, json, ok) {
            if (word !== shell.dictWord) return          // ignore a stale/late reply
            var entries = ok ? L.dictParse(json) : []
            shell.dictEntries = entries
            shell.dictState = (entries && entries.length > 0) ? "ok" : "empty"
        }
    }

    // Resolve the saved resume position and open the book there. Read by the derived
    // key first; if that's empty, fall back to the RAW PATH key — the old reader wrote
    // some legacy entries under the literal path (get(book.id) then get(book.path)), so
    // mirroring that fallback lets those still resume. "" cfi = open at the start.
    function openAtResume(path) {
        var entry = Reader2Bridge.progressGet(shell.bookId)
        if (!entry || Object.keys(entry).length === 0)
            entry = Reader2Bridge.progressGet(path)       // raw-path fallback (old-reader parity)
        paper.open(path, L.resumeCfiOf(entry))
    }

    function openBook(path) { bookPath = path; if (paper.glueUp) shell.openAtResume(path) }
}
