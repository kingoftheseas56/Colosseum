// ReaderShell.qml — the reader component Biblio embeds on swap day (Task 16).
//
// Composition: the web Paper on the bottom, the native ReaderChrome (glass over
// paper — TASK 7) on top. The chrome stays hidden while you read and returns only when
// you reach for the top/bottom edge (or double-click / the book-open orientation beat),
// turns pages at the edges, and scrubs the gold rail; ReaderShell owns the wiring to
// the paper + the native stores. Keyboard turns (Right/Space/PageDown → next,
// Left/PageUp → prev, Esc → back) stay on this FocusScope and NEVER wake the chrome
// (keys are not routed to the reveal reducer — the whole point of the naked surface).
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

    // ---- selection / highlight view-model (Task 9, the pen — Round 1) ----
    // Stashed from the paper's 'selection' event; drive the native SelectionMenu popover.
    property var selRect: ({ x: 0, y: 0, w: 0, h: 0 })
    property string selText: ""
    property string selCfi: ""
    property bool selMenuShown: false

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
        paper.clearSelection()
    }

    // Persist a highlight in the chosen color and paint it on the paper. The record shape
    // satisfies BOTH readers: LeftPanel.highlightRow reads cfi from value|cfi|locator.cfi,
    // plus color/text/note/chapterLabel; the glue's addHighlight needs { id, cfi, color }.
    // annotationsSave (→ BookStores::listSave) STAMPS a fresh id when absent AND returns
    // the stored record, so we don't double-stamp — we read the id back from it and hand
    // that SAME id to the paper so re-apply/remove stay consistent across a reopen.
    function applyHighlight(color) {
        if (shell.bookPath === "" || shell.selCfi === "") { shell.dismissSelectionMenu(); return }
        var rec = {
            cfi: shell.selCfi,          // read by highlightRow (and by us for addHighlight)
            value: shell.selCfi,        // the glue's annotation identity field (belt + suspenders)
            color: color,
            text: shell.selText,
            note: "",
            chapterLabel: shell.chapterLabel
        }
        var saved = Reader2Bridge.annotationsSave(shell.bookId, rec)   // stamps id, returns record
        var id = (saved && saved.id !== undefined && saved.id !== null) ? String(saved.id) : ""
        if (id !== "") paper.addHighlight({ id: id, cfi: shell.selCfi, color: color })
        shell.refreshMarks()                                          // Highlights pane picks it up
        shell.dismissSelectionMenu()
    }

    // Ratified default paper appearance (Night) — pushed at open so the FIRST paint
    // already matches the mock. The full adjustable panel is Task 10; this is just the
    // correct default. (Shape mirrors paper_glue.js: theme{bg,fg}/font/sizePx/lineHeight/
    // marginPx/justify.)
    readonly property var defaultAppearance: ({
        theme: { bg: "#111013", fg: "#eee9de" },
        font: "book", sizePx: 18, lineHeight: 1.6, marginPx: 72, justify: true
    })

    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Right || e.key === Qt.Key_Space || e.key === Qt.Key_PageDown) { paper.next(); e.accepted = true }
        else if (e.key === Qt.Key_Left || e.key === Qt.Key_PageUp) { paper.prev(); e.accepted = true }
        // Esc: dismiss the selection popover first; else close the left panel; else the book.
        else if (e.key === Qt.Key_Escape) {
            if (shell.selMenuShown) shell.dismissSelectionMenu()
            else if (chrome.panelOpen) chrome.closePanel()
            else shell.closed()
            e.accepted = true
        }
    }

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
            } else if (name === "toggleChrome") {
                // double-click on EMPTY paper space (glue) → toggle the chrome reveal.
                chrome.toggle()
            } else if (name === "selection") {
                // text selected in the paper → stash it + open the SelectionMenu at its rect.
                // A new selection while the menu is up simply re-stashes (the menu repositions).
                shell.selText = (p.text !== undefined && p.text !== null) ? String(p.text) : ""
                shell.selCfi = (p.cfi !== undefined && p.cfi !== null) ? String(p.cfi) : ""
                shell.selRect = (p.rect && typeof p.rect === "object") ? p.rect : ({ x: 0, y: 0, w: 0, h: 0 })
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

    // The selection popover (Task 9, the pen — Round 1). Declared LAST so it floats above
    // the chrome; bridge-free, driven by shell.sel* and reporting back via signals. Its own
    // backdrop dismisses on tap-outside; ReaderShell does the save/paint/copy.
    SelectionMenu {
        id: selectionMenu
        anchors.fill: parent
        shown: shell.selMenuShown
        sel: shell.selRect
        onColorPicked: (color) => shell.applyHighlight(color)
        onCopyRequested: {
            if (shell.selText !== "") Clipboard.copy(shell.selText)   // exposed by main.cpp + the harness
            shell.dismissSelectionMenu()
        }
        onDismissed: shell.dismissSelectionMenu()
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
