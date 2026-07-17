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
        else if (e.key === Qt.Key_Escape) { shell.closed(); e.accepted = true }
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
                shell.chapterTicks = L.railTicks(p.toc, (p.toc && p.toc.length) ? p.toc.length : 0)
                paper.setAppearance(shell.defaultAppearance)      // ratified Night default
                chrome.wake()                                     // orientation beat: show briefly on open, recede after 3s idle
            } else if (name === "relocated" && shell.bookPath !== "") {
                // --- chrome view-model (rail + top bar) ---
                if (p.cfi !== undefined && p.cfi !== null) shell.lastCfi = String(p.cfi)
                if (p.chapterTitle !== undefined) shell.chapterLabel = String(p.chapterTitle)
                if (Number.isFinite(p.percent)) shell.percent = p.percent
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
        // Panels arrive in Tasks 8-11 / the pen in Task 9. The signals are WIRED now so
        // those tasks just fill in the target instead of re-plumbing the chrome.
        onSearchRequested: console.log("[shell] searchRequested (search sheet = Task 11)")
        onContentsRequested: console.log("[shell] contentsRequested (left panel = Task 8)")
        onAppearanceRequested: console.log("[shell] appearanceRequested (appearance panel = Task 10)")
        onBookmarkRequested: console.log("[shell] bookmarkRequested (the pen = Task 9)")
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
