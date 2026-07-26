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
import "../BiblioApi.js" as B    // pairKey(title, author) — the ONE derivation Biblio keys audiobooks by

FocusScope {
    id: shell
    property string bookPath: ""
    readonly property bool shellWindowed:
        typeof WindowMode !== "undefined" && WindowMode.shellWindowed
    // Store key = the SHA1[:20] fingerprint of the path, NOT the raw path. The old
    // reader keyed progress/bookmarks/annotations by this (BookBridge::progressKey);
    // deriving it here is what makes positions/marks survive the swap (zero migration).
    // Reader2Bridge.bookKey mirrors that derivation byte-for-byte (both delegate to
    // BookStores::keyFor, the single shared formula).
    property string bookId: bookPath === "" ? "" : Reader2Bridge.bookKey(bookPath)
    signal closed()
    // Minimize (2026-07-18, Hemanth: "books should minimize too"): the embedder parks the
    // book as a taskbar session instead of closing it. Same flush-first discipline as goBack.
    signal minimized()
    signal fullscreenRequested()
    focus: true

    // ---- debug logging (Part C5) — OFF by default so a shipped embedding stays quiet; the
    // standalone harness flips it on. Gates the [shell]/[paper] event tracing (never ships spam).
    property bool readerDebug: false

    // ---- cross-book generation guard (Part B1; reworked in re-review #2) — QML ISSUES the
    // per-open `gen`: openAtResume bumps currentGen and passes it into paper.open, and the glue
    // echoes it on every book-scoped emit ('ready'/'relocated'/'error'/'searchResults'/
    // 'footnote'/'selection'/'highlightTapped'). Gates: 'ready' is honored only on EXACT gen
    // match (L.acceptReady — the old adopt-a-newer-ready rule let a queued superseded 'ready'
    // re-arm bookReady mid-switch); display/save events need bookReady AND a non-stale gen
    // (L.acceptBookEvent); 'error' routes through L.errorDisposition. A late event from book A
    // in flight over QWebChannel can't land in ANY window after we switched to B.
    property int currentGen: -1
    // Has the CURRENT open reached 'ready'? Gates the failed-open surface (Part B3): an
    // 'error' BEFORE ready = the book won't open (show the surface); after ready it's an
    // operational error (a failed search/highlight) that just traces.
    property bool bookReady: false
    property bool openErrorShown: false
    property string openErrorText: ""

    // ---- progress-save debounce (Part B4) — coalesce rapid page turns into ONE store write
    // (~60ms after the last turn) instead of a JSON read+write per turn. The pending relocated
    // + its book identity are stashed; the timer (or an explicit flush on close / book switch)
    // writes it. Capturing id/path at stash time keeps a flush correct across a book change.
    property var pendingSave: null
    property string pendingSaveId: ""
    property string pendingSaveBookPath: ""

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
    // Is the current page real prose (vs a cover / full-image page)? From relocated.textPage.
    // The reading ruler only shows on text pages — a focus band over a cover image is a bug.
    property bool currentPageIsText: true
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

    // Dismiss the selection popover and drop the paper's live selection. Always hand
    // keyboard focus BACK to the web view: the Note editor grabs Qt focus (a TextEdit),
    // and hiding it doesn't return focus on its own — so without this, page-turn keys +
    // Esc silently die after using Note until you click the text. (The color/Copy/Delete
    // paths use MouseAreas, which never take focus, so there this is a harmless no-op.)
    function dismissSelectionMenu() {
        shell.selMenuShown = false
        shell.selMenuMode = "select"
        shell.existingHlId = ""
        paper.clearSelection()
        Qt.callLater(function () { paper.focusPaper() })
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

    // ---- appearance (Task 10) ----
    // The CURRENT reader2 appearance (theme/font/size/lineHeight/margins/justify + the ruler
    // CONTROLS). Loaded from the SHARED settings.json `reader2` sub-object on 'ready' (courtesy-
    // seeded from the old reader's flat `theme` on first run), pushed to the paper as the first
    // paint, and re-pushed LIVE on every panel edit. Seeded to the ratified defaults until 'ready'.
    property var appearance: L.appearanceDefaults()
    // The WHOLE appearance store (PARITY 2026-07-24): { defaults, books } — per-book sparse
    // patches over a global default. `appearance` above stays THE effective object for this
    // book (what the panel binds and the paper renders); the store is the persistence truth.
    property var appearanceStore: ({ defaults: L.appearanceDefaults(), books: {} })

    // ---- search view-model (Task 11) ----
    // Fed by the paper's 'searchResults' event and handed to the SearchSheet through the
    // chrome. searchLastQuery is set only when results ARRIVE (not at submit), so the sheet's
    // empty state reads "Type to search" until a search returns, then "No results" if empty.
    property var searchResults: []
    property int searchCount: 0
    property bool searchCapped: false
    property string searchLastQuery: ""

    // Reset the search view-model (used when the sheet opens and when it closes).
    function resetSearch() {
        shell.searchResults = []
        shell.searchCount = 0
        shell.searchCapped = false
        shell.searchLastQuery = ""
    }

    // ---- read-along (Task 13) — the Audio tab + page/chapter sync ----
    // The ONE app-wide AudiobookSession is INJECTED by the embedder (Harness.qml here,
    // Main.qml on swap day): "one engine, many faces" (Hemanth 2026-07-13) — the reader is
    // a REMOTE that drives the shared session, never a second player. Null in a bare
    // instance; every read/drive below is guarded.
    property var audioSession: null
    // "Follow my reading" — OFF by default; each book open resets it (set on 'ready').
    property bool followOn: false
    // Remember the last audiobook chapter we synced to, so a debounced page turn that maps
    // to the SAME chapter is a no-op (an m4b's currentIndex stays 0, so we can't rely on it).
    property int lastSyncedAudioChapter: -1

    // ---- read-along (Task 6): sentence/word alignment driven by the native engine ----
    // THE DORMANT GATE (load-bearing): the native ReadAlongController (`ReadAlong`) and
    // AudioTextAlignmentService (`AudioTextAlignment`) are registered in Task 12, NOT yet.
    // Until then this flag is false and EVERY read-along binding/handler/connection below is
    // gated by it, so the live reader behaves EXACTLY as it does today. `typeof X !== 'undefined'`
    // is the proven in-repo probe for an un-registered context property (same as AudioPairing/
    // Audiobooks/Progress above); it never throws when the prop is absent.
    readonly property bool readAlongAvailable:
        (typeof ReadAlong !== "undefined") && (typeof AudioTextAlignment !== "undefined")
    // The persisted mode/enlargement ride in the SAME reader2 appearance store (appearance.readAlong).
    readonly property string readAlongMode: L.readAlongFrom(shell.appearance).mode
    readonly property real readAlongWordScale: L.readAlongFrom(shell.appearance).wordScale
    // Controller follow state mirror ("following"|"detached"); updated by followStateChanged.
    property string readAlongFollowState: "following"
    readonly property bool readAlongFollowDetached: shell.readAlongAvailable && shell.readAlongFollowState === "detached"
    // The last playhead identity we fed the controller — skip re-feeding an unchanged one.
    property var lastPlayhead: null
    // A committed seek's follow-up navigationRequested is a hard jump (navigate); a passive
    // follow move is a comfort-zone ensureVisible. This one-shot flag distinguishes them.
    property bool pendingReadAlongJump: false
    // Scrub-preview read-out (the aligned rail): active while dragging + the controller's label.
    property bool readAlongPreviewActive: false
    property string readAlongPreviewLabel: ""
    // Real audiobook chapter start offsets (ms) arrive in Task 12; null → nominal (position-as-
    // absolute, correct for the single-file m4b read-along case). Pure conversions consume it.
    property var audioChapterBoundsMs: null

    // The pairing Task 12 wrote for THIS book (keyed by shell.bookId). Reactive: it
    // re-evaluates when the book changes AND when the store bumps (an audiobook that
    // finishes downloading while the book is open auto-attaches → revision changes).
    property int audioPairingRev: (typeof AudioPairing !== "undefined") ? AudioPairing.revision : 0
    property var audioPairing: shell.lookupPairing(shell.bookId, shell.audioPairingRev)
    function lookupPairing(id, rev) {
        if (id === "" || typeof AudioPairing === "undefined") return ({})
        return AudioPairing.getPairing(id) || ({})
    }
    readonly property string audioPairKey: (shell.audioPairing && shell.audioPairing.pairKey)
                                           ? String(shell.audioPairing.pairKey) : ""
    // Attached = a pairing exists AND its audiobook is actually on disk (a stale pairing
    // whose files were deleted must fall back to the unattached state, never a dead card).
    readonly property bool audioAttached: shell.audioPairKey !== ""
        && typeof Audiobooks !== "undefined" && Audiobooks.isDownloaded(shell.audioPairKey)

    // The downloaded-audiobook index entry (title/author/fileCount) for the card BEFORE
    // the session loads. Recomputed on the same triggers as the pairing.
    property var audioEntry: shell.findAudioEntry(shell.audioPairKey, shell.audioPairingRev)
    function findAudioEntry(pk, rev) {
        if (pk === "" || typeof Audiobooks === "undefined") return ({})
        var list = Audiobooks.downloadedAudiobooks() || []
        for (var i = 0; i < list.length; i++)
            if (String(list[i].id) === pk) return list[i]
        return ({})
    }

    // The session is LIVE for THIS book when it's loaded and streaming our pairKey (it may
    // be streaming a DIFFERENT book — another reader/full player — in which case our
    // transport shows the idle state, never the other book's position).
    readonly property bool audioSessionLive: !!(shell.audioSession && shell.audioSession.ready
                                                && shell.audioSession.activePairKey === shell.audioPairKey
                                                && shell.audioPairKey !== "")

    // ---- card + transport props (fed to the chrome → LeftPanel Audio pane) ----
    readonly property string audioTitle:
        (shell.audioSessionLive && shell.audioSession.book && shell.audioSession.book.title)
            ? String(shell.audioSession.book.title)
        : (shell.audioEntry && shell.audioEntry.title) ? String(shell.audioEntry.title)
        : (shell.bookTitle !== "" ? shell.bookTitle : "Audiobook")
    readonly property url audioCover:
        (shell.audioSessionLive && shell.audioSession.book && shell.audioSession.book.cover)
            ? shell.audioSession.book.cover : ""
    readonly property int audioChapterCount:
        (shell.audioSessionLive && shell.audioSession.chapterModel)
            ? shell.audioSession.chapterModel.length
        : (shell.audioEntry && Number.isFinite(shell.audioEntry.fileCount)) ? shell.audioEntry.fileCount : 0
    // A total duration is only trustworthy for a single-file m4b (mpv knows the whole
    // book); a multi-file set would need every file probed, so we omit hours there.
    readonly property real audioTotalSec:
        (shell.audioSessionLive && !shell.audioSession.multiFile && shell.audioSession.duration > 0)
            ? shell.audioSession.duration : 0
    readonly property string audioMetaLine: L.audiobookMetaLine(shell.audioChapterCount, shell.audioTotalSec)

    readonly property string currentAudioChapterLabel:
        (shell.audioSessionLive && shell.audioSession.chapterModel
         && shell.audioSession.currentIndex >= 0
         && shell.audioSession.currentIndex < shell.audioSession.chapterModel.length)
            ? String(shell.audioSession.chapterModel[shell.audioSession.currentIndex].label || "")
            : ""
    readonly property bool audioPlaying: shell.audioSessionLive && !shell.audioSession.paused
    readonly property string audioTimeLine: shell.audioSessionLive
        ? L.audiobookTimeLine(shell.currentAudioChapterLabel, shell.audioSession.position, shell.audioSession.duration)
        : "Press play to listen along"
    readonly property real audioProgress: (shell.audioSessionLive && shell.audioSession.duration > 0)
        ? shell.audioSession.position / shell.audioSession.duration : 0
    // The pill's scrub-rail flanks (TB2 parity): elapsed / total of the CURRENT stream.
    readonly property string audioPosLabel: shell.audioSessionLive ? L.fmtClock_(shell.audioSession.position) : ""
    readonly property string audioDurLabel: (shell.audioSessionLive && shell.audioSession.duration > 0)
        ? L.fmtClock_(shell.audioSession.duration) : ""
    // Numeric total (seconds) for the HUD scrub-tooltip's time readout (same source as audioDurLabel).
    readonly property real audioDurationSec: (shell.audioSessionLive && shell.audioSession.duration > 0)
        ? shell.audioSession.duration : 0
    // CHOSEN speed (Hemanth 2026-07-18: "the speed button does not do anything" — it only
    // acted on a LIVE stream). The choice now lives shell-side: cycling always works and
    // shows, and it's applied to the session on load and on every cycle while live.
    property real audioChosenSpeed: 1.0
    readonly property string audioSpeedLabel: L.speedLabel(shell.audioChosenSpeed)

    // ---- driving the shared session ----
    // A light book object for the session's Continue record (title/author).
    function audioSessionBook() {
        var e = shell.audioEntry || ({})
        return {
            title: (e.title ? String(e.title) : (shell.bookTitle !== "" ? shell.bookTitle : "Audiobook")),
            author: (e.author ? String(e.author) : shell.bookAuthor),
            cover: ""
        }
    }
    // Load the paired audiobook into the shared session if it isn't already streaming for
    // this book. openFor is IDEMPOTENT (same pairKey already live → no-op, stream untouched).
    // startPaused=true lands at the last spot paused (the Follow-on summon); false plays.
    function ensureAudioLoaded(startPaused) {
        if (!shell.audioSession || shell.audioPairKey === "") return false
        if (typeof Audiobooks !== "undefined" && !Audiobooks.isDownloaded(shell.audioPairKey)) return false
        shell.audioSession.openFor(shell.audioPairKey, shell.audioSessionBook(), startPaused === true)
        if (shell.audioSession.setRate) shell.audioSession.setRate(shell.audioChosenSpeed)  // carry the chosen speed in
        return !!shell.audioSession.ready
    }

    // The speed cycle (AudiobookPlayer's ladder), shared by the transport pill.
    readonly property var audioSpeeds: [1.0, 1.25, 1.5, 1.75, 2.0, 0.75]

    // Debounced read-along sync: a page turn restarts this; on fire it snaps the audiobook
    // to the chapter matching the current page (once, not per intermediate flip).
    Timer {
        id: followSyncTimer
        interval: 400
        onTriggered: shell.syncAudioToPage(false)
    }
    // Snap the audiobook to the chapter matching the current book page. Guards: Follow must
    // be on and the session live for this book; an unknown/degenerate match (-1) is skipped;
    // a match equal to the last-synced chapter is a no-op (no redundant seek). Preserves the
    // play/pause state (goToChapterKeepState) so following never force-plays a paused stream.
    function syncAudioToPage(immediate) {
        if (!shell.followOn || !shell.audioSessionLive) return
        var chapters = shell.audioSession.chapterModel || []
        var ch = L.chapterFor(shell.currentTocIndex, shell.bookToc, chapters)
        if (ch < 0) return
        if (ch === shell.lastSyncedAudioChapter && !immediate) return
        shell.lastSyncedAudioChapter = ch
        shell.audioSession.goToChapterKeepState(ch)
    }
    // Follow switch → set state; turning ON loads (paused at last spot). When read-along is
    // AVAILABLE the direction inverts (Task 6): the PAGE follows the AUDIO via the controller
    // (feedPlayhead → paint), so we attach the controller instead of the old chapter-sync.
    // DORMANT keeps today's behavior exactly (audio follows the page: syncAudioToPage).
    function setFollow(on) {
        shell.followOn = on
        if (on) {
            shell.lastSyncedAudioChapter = -1        // force the first sync through (dormant path)
            shell.ensureAudioLoaded(true)
            if (shell.readAlongAvailable) {
                ReadAlong.returnToNarration()        // attach the controller to the live narration
                shell.readAlongFollowState = "following"
                shell.lastPlayhead = null
                shell.feedPlayhead()                 // seed it with the current audio position
            } else {
                shell.syncAudioToPage(true)          // dormant: audio follows the page (today)
            }
        } else if (shell.readAlongAvailable) {
            ReadAlong.detachFollow()                 // stop driving the page from audio
        }
    }

    // ---- read-along wiring (Task 6) — all gated on readAlongAvailable (dormant = untouched) ----
    // feedPlayhead: playback → controller. Only while following AND the controller is attached
    // ("following", not "detached" after a manual navigation). Converts (currentIndex, position)
    // to an absolute-ish audiobook ms and skips an unchanged playhead (pure decisions in L.*).
    function feedPlayhead() {
        if (!shell.readAlongAvailable || !shell.followOn) return
        if (shell.readAlongFollowState !== "following") return
        if (!shell.audioSessionLive) return
        var idx = shell.audioSession.currentIndex
        var absMs = L.sessionToAbsMs(idx, shell.audioSession.position, shell.audioChapterBoundsMs)
        var next = { chapter: idx, absMs: absMs }
        if (!L.shouldEmitSetPlayhead(shell.lastPlayhead, next)) return
        shell.lastPlayhead = next
        ReadAlong.setPlayhead(shell.bookId, idx, absMs)
    }

    // dispatchScrub: the gold scrub rail's preview (hover/drag) / commit (release). L decides:
    // when available → previewTime (NO seek) / commitTime (one commit); when dormant → a plain
    // direct seek on both, preserving today's continuous-scrub feel.
    function dispatchScrub(phase, f) {
        var dur = (shell.audioSession && Number.isFinite(shell.audioSession.duration)) ? shell.audioSession.duration : 0
        var idx = shell.audioSession ? shell.audioSession.currentIndex : 0
        var act = L.readAlongScrubAction(phase, f, dur, shell.readAlongAvailable)
        if (act.kind === "preview") {
            ReadAlong.previewTime(shell.bookId, idx, act.timeMs)
            shell.readAlongPreviewActive = true
        } else if (act.kind === "commit") {
            ReadAlong.commitTime(shell.bookId, idx, act.timeMs)   // → controller audioSeekRequested → ONE seek
            shell.readAlongPreviewActive = false
            shell.pendingReadAlongJump = true                     // the follow-up nav is a hard jump
        } else {
            shell.audioSeekFraction(f)                            // dormant: today's direct seek
        }
    }

    // performControllerSeek: the controller's audioSeekRequested is the ONE place a read-along
    // seek actually touches the session (every user gesture funnels through commitX → this).
    function performControllerSeek(chapter, timeMs, play) {
        if (!shell.audioSession) return
        shell.pendingReadAlongJump = true                         // its navigationRequested navigates
        var n = (shell.audioSession.chapterModel || []).length
        if (Number.isFinite(chapter) && chapter >= 0 && chapter < n && chapter !== shell.audioSession.currentIndex)
            shell.audioSession.goToChapterKeepState(chapter)
        shell.audioSession.seekTo(L.audioSeekTargetSec(chapter, timeMs, shell.audioChapterBoundsMs))
        if (play === true) shell.audioSession.play()
        shell.lastPlayhead = null                                 // a jump re-seeds the feed
    }

    // A single read-along mode/enlargement edit from the panel: merge into appearance.readAlong,
    // PERSIST under the same namespaced settings.reader2 (read-modify-write, never clobbering the
    // old reader's flat keys or the appearance fields), and LIVE-STYLE the paper.
    function applyReadAlongPatch(key, value) {
        var patch = {}
        patch[key] = value
        shell.appearance = L.mergeReadAlong(shell.appearance, patch)
        shell.appearanceStore = L.applyStorePatch(shell.appearanceStore, shell.bookId,
                                                  "readAlong", L.readAlongFrom(shell.appearance))
        persistAppearanceStore()
        if (shell.readAlongAvailable)
            paper.setReadAlongStyle(L.readAlongStyleFromMode(shell.readAlongMode, shell.readAlongWordScale))
    }
    // Return to narration: re-attach the controller and re-seed it with the current playhead.
    function returnToNarration() {
        if (!shell.readAlongAvailable) return
        ReadAlong.returnToNarration()
        shell.readAlongFollowState = "following"
        shell.lastPlayhead = null
        shell.feedPlayhead()
    }

    // Controller → shell: paint/navigate the paper, drive the session seek, mirror follow state.
    // The target guard (readAlongAvailable ? ReadAlong : null) means the ReadAlong id is NEVER
    // referenced when the prop is absent — dormant instances bind to a null target (inert).
    Connections {
        target: shell.readAlongAvailable ? ReadAlong : null
        function onPaintRequested(cue) { paper.paintReadAlong(cue) }
        function onNavigationRequested(location) {
            if (shell.pendingReadAlongJump) { shell.pendingReadAlongJump = false; paper.navigateReadAlong(location) }
            else paper.ensureReadAlongVisible(location)          // comfort-zone follow
        }
        function onAudioSeekRequested(chapter, timeMs, play) { shell.performControllerSeek(chapter, timeMs, play) }
        function onFollowStateChanged() { shell.readAlongFollowState = ReadAlong.followState }
        function onPreviewChanged() { shell.readAlongPreviewLabel = L.previewLabelFrom(ReadAlong.preview).line }
    }
    // Playback → controller: feed the playhead as the audio position/chapter/pause changes.
    Connections {
        target: shell.audioSession
        enabled: shell.readAlongAvailable
        function onPositionChanged() { shell.feedPlayhead() }
        function onCurrentIndexChanged() { shell.feedPlayhead() }
        function onPausedChanged() { shell.feedPlayhead() }
    }
    // Play/pause from the transport: toggle a live stream, else load-and-play this book's audiobook.
    function audioPlayToggle() {
        if (!shell.audioSession) return
        if (shell.audioSessionLive) shell.audioSession.togglePlay()
        else shell.ensureAudioLoaded(false)          // load + play
    }
    // Cycle the playback speed — works BEFORE play too (the choice persists shell-side
    // and is applied when the stream loads; live streams re-rate immediately).
    function audioCycleSpeed() {
        var s = shell.audioSpeeds, cur = shell.audioChosenSpeed, next = s[0]
        for (var i = 0; i < s.length; i++)
            if (Math.abs(s[i] - cur) < 0.01) { next = s[(i + 1) % s.length]; break }
        shell.audioChosenSpeed = next
        if (shell.audioSessionLive) shell.audioSession.setRate(next)
    }
    // Chapter transport (the pill's ⏮ ⏭): live → clamped jump keeping play/pause state;
    // not live yet → load the stream (the jump lands via the pending-index seam below).
    function audioPrevChapter() {
        if (!shell.audioSessionLive) { shell.ensureAudioLoaded(false); return }
        var i = shell.audioSession.currentIndex
        if (i <= 0) return
        // While SYNCING (read-along on + following), a chapter skip converges on a controller
        // commit — one seek, and the page jumps to the new chapter's narration. Else: today.
        if (shell.readAlongAvailable && shell.followOn)
            ReadAlong.commitTime(shell.bookId, i - 1, L.sessionToAbsMs(i - 1, 0, shell.audioChapterBoundsMs))
        else shell.audioSession.goToChapterKeepState(i - 1)
    }
    function audioNextChapter() {
        if (!shell.audioSessionLive) { shell.ensureAudioLoaded(false); return }
        var i = shell.audioSession.currentIndex
        var n = (shell.audioSession.chapterModel || []).length
        if (i >= n - 1) return
        if (shell.readAlongAvailable && shell.followOn)
            ReadAlong.commitTime(shell.bookId, i + 1, L.sessionToAbsMs(i + 1, 0, shell.audioChapterBoundsMs))
        else shell.audioSession.goToChapterKeepState(i + 1)
    }
    // ---- the playlist (Audio tab lists every chapter/file like Contents lists chapters) ----
    // Live session → its chapterModel labels (m4b embedded chapters or the file set).
    // Not live → the downloaded set's FILENAMES (Hemanth: "with the filenames"), so the
    // list is there before the first play. audioPairingRev keeps it fresh across heals.
    readonly property var audioPlaylist: (shell.audioPairingRev, (function() {
        if (shell.audioSessionLive) {
            var ch = shell.audioSession.chapterModel || []
            return ch.map(function(c) { return String(c.label || "") })
        }
        if (!shell.audioAttached || typeof Audiobooks === "undefined") return []
        return Audiobooks.localFiles(shell.audioPairKey).map(function(p) {
            var base = String(p).split(/[\\/]/).pop()
            return base.replace(/\.[a-z0-9]+$/i, "")      // filename, extension shed
        })
    })())
    readonly property int audioCurrentIndex: shell.audioSessionLive ? shell.audioSession.currentIndex : -1
    // Tap a playlist row: live → jump-and-play; cold → load, then the pending index lands
    // once the session reports ready (openFor is async — a blind jump would race it).
    property int audioPendingJump: -1
    function audioPlayAt(i) {
        if (shell.audioSessionLive) {
            if (shell.readAlongAvailable && shell.followOn)
                ReadAlong.commitTime(shell.bookId, i, L.sessionToAbsMs(i, 0, shell.audioChapterBoundsMs))
            else shell.audioSession.goToChapter(i)
            return
        }
        shell.audioPendingJump = i
        shell.ensureAudioLoaded(false)
    }
    Connections {
        target: shell.audioSession
        function onReadyChanged() {
            if (shell.audioPendingJump >= 0 && shell.audioSessionLive) {
                var i = shell.audioPendingJump
                shell.audioPendingJump = -1
                if (i > 0) shell.audioSession.goToChapter(i)   // 0 = where openFor already landed
            }
        }
    }
    // Scrub the mini rail (fraction 0..1 → absolute seek).
    function audioSeekFraction(f) {
        if (shell.audioSessionLive && shell.audioSession.duration > 0)
            shell.audioSession.seekTo(shell.audioSession.duration * Math.max(0, Math.min(1, f)))
    }
    // volume rides the session's mpv (0..100 linear, player-house rule); the chrome speaks 0..1.
    // Dragging the rail above zero also unmutes — the player's "volume > 0 clears mute" manner.
    readonly property real audioVolume: shell.audioSessionLive
        ? Math.max(0, Math.min(1, shell.audioSession.volume / 100)) : 1
    readonly property bool audioMuted: shell.audioSessionLive ? !!shell.audioSession.mute : false
    function audioSetVolume(f) {
        if (!shell.audioSessionLive) return
        shell.audioSession.volume = Math.round(Math.max(0, Math.min(1, f)) * 100)
        if (shell.audioSession.volume > 0) shell.audioSession.mute = false
    }
    function audioToggleMute() {
        if (shell.audioSessionLive) shell.audioSession.mute = !shell.audioSession.mute
    }
    // Relative skip (±seconds) from the HUD transport pill. While syncing it converges on a
    // controller commit (the paint follows); dormant / not-following keeps today's direct seek.
    function audioSkip(sec) {
        if (!shell.audioSessionLive) return
        if (shell.readAlongAvailable && shell.followOn) {
            var absMs = L.sessionToAbsMs(shell.audioSession.currentIndex,
                                         Math.max(0, shell.audioSession.position + sec),
                                         shell.audioChapterBoundsMs)
            ReadAlong.commitTime(shell.bookId, shell.audioSession.currentIndex, absMs)
        } else {
            shell.audioSession.seekTo(Math.max(0, shell.audioSession.position + sec))
        }
    }

    // Commit a new store: recompute this book's effective appearance, persist, and paint.
    // The single sync point for the three appearance actions (patch / use-as-default / reset).
    function commitAppearanceStore(newStore) {
        shell.appearanceStore = newStore
        shell.appearance = L.effectiveAppearance(newStore, shell.bookId)
        persistAppearanceStore()
        paper.setAppearance(L.appearanceToPaper(shell.appearance))
    }

    // A single appearance edit from the panel: route it through the STORE (applyStorePatch
    // tiers GLOBAL keys → defaults, others → this book's sparse patch), then commit. The ruler
    // fields ride along in the store but are ignored by appearanceToPaper — their overlay is Task 11.
    function applyAppearancePatch(key, value) {
        commitAppearanceStore(L.applyStorePatch(shell.appearanceStore, shell.bookId, key, value))
    }

    // "Use as default for all books": this book's look becomes the global default; books
    // Hemanth personally tuned keep their own patches.
    function applyUseAsDefault() {
        commitAppearanceStore(L.useAsDefaultStore(shell.appearanceStore, shell.bookId))
    }

    // "Reset appearance": drop this book's patch — it falls back to the default look.
    function applyResetBook() {
        commitAppearanceStore(L.resetBookStore(shell.appearanceStore, shell.bookId))
    }

    // Persist the whole store under settings.reader2 (READ-MODIFY-WRITE — the OLD reader's
    // flat keys elsewhere in settings.json are never clobbered).
    function persistAppearanceStore() {
        var all = Reader2Bridge.settingsGet() || ({})
        all.reader2 = shell.appearanceStore
        Reader2Bridge.settingsSave(all)
    }

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
        // When the search sheet closes (Esc, toggle, tap a panel), drop the paper's search
        // highlight, reset the view-model, and hand keyboard focus BACK to the web view — the
        // sheet's TextInput held Qt focus (like the Note editor), so without this, page-turn
        // keys + Esc would silently die until you click the page.
        function onSearchOpenChanged() {
            if (!chrome.searchOpen) {
                paper.clearSearch()
                shell.resetSearch()
                Qt.callLater(function () { paper.focusPaper() })
            }
        }
    }

    // Debounce that coalesces rapid page-turn saves (Part B4): restarted on each relocated,
    // fires ~60ms after the LAST turn to write the final position once.
    Timer {
        id: progressSaveTimer
        interval: 60
        onTriggered: shell.flushProgressSave()
    }
    // Write the pending progress NOW (timer fire, or an explicit flush on close / book switch).
    // No-op when nothing is pending. Same read-prev + L.progressRecord path as before; only the
    // TIMING moved off the per-event hot path.
    function flushProgressSave() {
        progressSaveTimer.stop()
        var pend = shell.pendingSave
        if (!pend || shell.pendingSaveId === "") { shell.pendingSave = null; return }
        var id = shell.pendingSaveId
        var prev = Reader2Bridge.progressGet(id)
        Reader2Bridge.progressSave(id, L.progressRecord(prev, pend, shell.pendingSaveBookPath))
        // Feed the unified home Continue/resume row (old-reader parity — the swap dropped this
        // wire, so Continue went stale and never learned about fresh-reader sessions). Same
        // record shape the old reader wrote; resume carries {path, book} for openBookSession.
        // bookMeta is host-injected; the standalone harness has none and this quietly skips.
        if (typeof Progress !== "undefined" && shell.pendingSaveBookPath !== "") {
            var m = shell.bookMeta || ({})
            var fraction = Number.isFinite(pend.fraction) ? Math.min(1, Math.max(0, pend.fraction)) : 0
            Progress.record({
                "id": (m.id !== undefined && ("" + m.id).length) ? ("" + m.id) : shell.pendingSaveBookPath,
                "kind": "book",
                "caption": m.title || shell.bookTitle || "",
                "title": m.title || shell.bookTitle || "",
                "sub": (fraction > 0 ? Math.round(fraction * 100) + "%" : "Reading"),
                "cover": m.cover || "",
                "c1": m.c1 !== undefined ? m.c1 : "#2a2440",
                "c2": m.c2 !== undefined ? m.c2 : "#15111f",
                "progress": fraction,
                "resume": { "path": shell.pendingSaveBookPath, "book": m }
            })
        }
        shell.pendingSave = null
    }
    // Leave the reader: FLUSH any pending save first (so a page turn within the debounce window
    // isn't lost when the book closes), then tell the embedder. Used everywhere we'd emit closed().
    function goBack() { shell.flushProgressSave(); shell.closed() }
    // Minimize: flush the position, then let the embedder park the session (the resume seam
    // brings the book back to this exact page when the taskbar tile reopens it).
    function goMinimize() { shell.flushProgressSave(); shell.minimized() }
    Component.onDestruction: shell.flushProgressSave()

    Paper {
        id: paper
        anchors.fill: parent
        readerDebug: shell.readerDebug
        onGlueUpChanged: if (glueUp && shell.bookPath !== "") shell.openAtResume(shell.bookPath)
        onPaperEvent: (name, p) => {
            if (shell.readerDebug) console.log("[shell]", name, JSON.stringify(p).slice(0, 160))

            if (name === "ready") {
                // Cross-book guard (re-review #2 rework): the only 'ready' honored is the one
                // stamped with the gen THIS shell issued in openAtResume (exact match — see
                // L.acceptReady). The old adopt-a-newer-ready rule is dead: a queued 'ready'
                // from a superseded slow open could carry a newer-than-adopted gen, get adopted,
                // and re-arm bookReady mid-switch. No adoption happens here anymore — currentGen
                // was set at issue time.
                if (!L.acceptReady(p.gen, shell.currentGen)) return
                shell.bookReady = true                            // opened OK → later 'error' is operational, not a failed open
                shell.openErrorShown = false                      // clear any failed-open surface from a prior attempt
                // book identity + toc arrive here (before the first relocate).
                shell.bookTitle = (p.metadata && p.metadata.title) ? String(p.metadata.title) : ""
                shell.bookAuthor = L.authorText(p.metadata)
                shell.bookToc = (p.toc && Array.isArray(p.toc)) ? p.toc : []
                shell.currentTocIndex = -1                        // unknown until the first relocate
                shell.chapterTicks = L.railTicks(p.toc, (p.toc && p.toc.length) ? p.toc.length : 0)
                shell.refreshMarks()                              // load bookmarks/highlights from the shared stores
                shell.reapplyHighlights()                         // re-paint stored highlights onto the fresh paper
                // PARITY: adopt the store (legacy flat reader2 migrates silently), then paint
                // THIS book's effective appearance — defaults overlaid by its own patch.
                shell.appearanceStore = L.appearanceStore(Reader2Bridge.settingsGet())
                shell.appearance = L.effectiveAppearance(shell.appearanceStore, shell.bookId)
                paper.setAppearance(L.appearanceToPaper(shell.appearance))           // first paint = the persisted appearance
                shell.followOn = false                            // read-along Follow resets per book (default OFF)
                shell.lastSyncedAudioChapter = -1                 // fresh book → no prior audio-chapter sync
                // read-along per-book reset (Task 6) — all no-ops when dormant.
                shell.readAlongFollowState = "following"
                shell.lastPlayhead = null
                shell.readAlongPreviewActive = false
                shell.pendingReadAlongJump = false
                if (shell.readAlongAvailable) {
                    paper.setReadAlongStyle(L.readAlongStyleFromMode(shell.readAlongMode, shell.readAlongWordScale))
                    // schedule alignment for the paired audiobook (chapter jobs run in the
                    // background worker; a no-op when there's no pairing yet — heals in Task 7/12).
                    if (typeof AudioTextAlignment !== "undefined" && shell.audioPairKey !== "")
                        AudioTextAlignment.ensurePair(shell.bookId, shell.bookPath, shell.audioPairKey)
                }
                chrome.wake()                                     // orientation beat: show briefly on open, recede after 3s idle
                paper.focusPaper()                                // the web view owns keys — focus it so keys work immediately
            } else if (name === "toggleChrome") {
                // double-click on EMPTY paper space (glue). If an overlay is open, dismiss it
                // first (same cascade as Esc: pen cards → selection popover → panel/search),
                // so a double-click also hides the search sheet / a panel. Otherwise toggle
                // the chrome reveal.
                if (shell.dictShown) shell.dismissDict()
                else if (shell.footnoteShown) shell.dismissFootnote()
                else if (shell.selMenuShown) shell.dismissSelectionMenu()
                else if (chrome.anyPanelOpen) chrome.closeAnyPanel()
                else chrome.toggle()
            } else if (name === "escape") {
                // Esc from the glue's in-page keyboard. Cascading close, same order the old
                // reader uses: the pen's floating cards first (dict/footnote), then the
                // selection popover, then the left panel, then the book.
                if (shell.dictShown) shell.dismissDict()
                else if (shell.footnoteShown) shell.dismissFootnote()
                else if (shell.selMenuShown) shell.dismissSelectionMenu()
                else if (chrome.anyPanelOpen) chrome.closeAnyPanel()   // left/right panel OR search sheet
                else shell.goBack()                                    // flushes the pending save, then closes
            } else if (name === "footnote") {
                // Superseded open OR the pre-ready window (openBook fired, new 'ready' not yet
                // adopted — currentGen still the OLD book's, so the gen check alone can't tell)
                // → drop. (Codex re-review fix; see L.acceptBookEvent.)
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return
                // A footnote/endnote link was tapped (the glue extracted its text). Show the
                // FootnoteCard near the anchor; the page does NOT navigate to the note.
                shell.footnoteText = (p.html !== undefined && p.html !== null) ? String(p.html) : ""
                shell.footnoteRect = (p.rect && typeof p.rect === "object") ? p.rect : ({ x: 0, y: 0, w: 0, h: 0 })
                if (shell.footnoteText !== "") shell.footnoteShown = true
                else if (shell.readerDebug) console.log("[shell] footnote had no extractable text — skipping card")
            } else if (name === "highlightTapped") {
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return  // pre-ready / stale — no popover over the wrong book
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
                // over a stale/empty selection. Gen-gated too (re-review #3): a queued clear
                // from the PREVIOUS book must not dismiss a menu the current book just opened.
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return
                if (shell.selMenuShown) shell.dismissSelectionMenu()
            } else if (name === "selection") {
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return  // pre-ready / stale — no popover over the wrong book
                // text selected in the paper → stash it + open the SelectionMenu at its rect.
                // A new selection while the menu is up simply re-stashes (the menu repositions).
                shell.selText = (p.text !== undefined && p.text !== null) ? String(p.text) : ""
                shell.selCfi = (p.cfi !== undefined && p.cfi !== null) ? String(p.cfi) : ""
                shell.selRect = (p.rect && typeof p.rect === "object") ? p.rect : ({ x: 0, y: 0, w: 0, h: 0 })
                shell.selMenuMode = "select"
                shell.existingHlId = ""
                shell.selMenuShown = (shell.selText !== "")
            } else if (name === "searchResults") {
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return  // superseded open or pre-ready window — drop
                // Hits from paper.search (the glue caps the payload at 300 + flags `capped`).
                // Stash them for the SearchSheet; set searchLastQuery HERE (on arrival) so the
                // sheet only shows "No results" once a search actually came back empty.
                shell.searchResults = (p.results && Array.isArray(p.results)) ? p.results : []
                shell.searchCount = Number.isFinite(p.count) ? p.count : shell.searchResults.length
                shell.searchCapped = !!p.capped
                shell.searchLastQuery = (p.query !== undefined && p.query !== null) ? String(p.query) : ""
            } else if (name === "error") {
                // Part B3 through L.errorDisposition (v2, QML-issued gens): only the error of
                // the open we ISSUED may act — pre-ready it's a failed open (surface), post-
                // ready it's operational (trace). Any other stamped gen is a superseded open's
                // error and drops; no adoption is needed since currentGen was set at issue time.
                var emsg = (p.message !== undefined && p.message !== null) ? String(p.message) : ""
                var disp = L.errorDisposition(p.gen, shell.currentGen, shell.bookReady)
                if (disp === "open-fail") {
                    shell.openErrorText = emsg
                    shell.openErrorShown = true
                } else if (disp === "operational" && shell.readerDebug) {
                    console.log("[shell] operational error:", emsg)
                }   // 'drop' → the superseded book's error; ignore
            } else if (name === "relocated" && shell.bookPath !== "") {
                // Superseded open OR pre-ready window → ignore. The pre-ready half is the save
                // that mattered most (Codex re-review): bookPath is already the NEW book's when
                // openBook ran, so an old book's in-flight relocated accepted here would record
                // ITS position under the new book's progress entry.
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return
                // --- chrome view-model (rail + top bar + Contents current row) ---
                if (p.cfi !== undefined && p.cfi !== null) shell.lastCfi = String(p.cfi)
                if (p.chapterTitle !== undefined) shell.chapterLabel = String(p.chapterTitle)
                if (Number.isFinite(p.percent)) shell.percent = p.percent
                if (Number.isFinite(p.tocIndex)) shell.currentTocIndex = p.tocIndex
                if (Number.isFinite(p.fraction)) shell.lastFraction = p.fraction
                if (typeof p.textPage === "boolean") shell.currentPageIsText = p.textPage
                if (Number.isFinite(p.pageInChapter)) { shell.pageInChapter = p.pageInChapter; shell.lastPageInChapter = p.pageInChapter }
                if (Number.isFinite(p.pagesInChapter)) shell.pagesInChapter = p.pagesInChapter

                // --- RESUME SEAM save (Task 6) — now DEBOUNCED (Part B4) ---
                // Stash the position + THIS book's identity and restart the 60ms timer, so a
                // burst of page turns writes the store once (a JSON read+write per turn was the
                // cost). Capturing id/path here (not at flush time) keeps a flush correct even if
                // the book switches before it fires. The .pragma logic can't touch Date, so we
                // stamp updatedAt now. flushProgressSave() (close / book switch) never loses the
                // final position.
                p.updatedAt = Date.now()
                shell.pendingSave = p
                shell.pendingSaveId = shell.bookId
                shell.pendingSaveBookPath = shell.bookPath
                progressSaveTimer.restart()

                // --- chapter-level follow (Task 13) — DORMANT-ONLY now (Task 6) ---
                // When read-along is available the direction inverts: the PAGE follows the
                // AUDIO via the controller (feedPlayhead), and USER page turns emit the paper's
                // 'manualNavigation' event → detachFollow. So the old "audio follows the page"
                // chapter-snap runs ONLY when read-along is absent — today's behavior, untouched.
                if (!shell.readAlongAvailable && shell.followOn && shell.audioSessionLive) followSyncTimer.restart()
            } else if (name === "alignedDoubleClick") {
                // Read-along (Task 6): a double-click that landed within a painted sentence/word.
                // Converge on a controller commit → it emits audioSeekRequested (ONE seek) +
                // navigationRequested (the page jump). Gated + gen-checked like other book events.
                if (!shell.readAlongAvailable) return
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return
                shell.pendingReadAlongJump = true
                ReadAlong.commitLocation(shell.bookId, p.location)
            } else if (name === "manualNavigation") {
                // A READER-initiated move (wheel/drag/page-turn/TOC/search/bookmark). Detach the
                // audio follow WITHOUT pausing — the paint freezes at the last trusted spot and
                // the "Return to narration" chip appears. Programmatic (controller) moves are
                // tagged in the glue and never emit this, so return-to-narration can't detach itself.
                if (!shell.readAlongAvailable) return
                if (!L.acceptBookEvent(p.gen, shell.currentGen, shell.bookReady)) return
                ReadAlong.detachFollow()
            } else if (name === "readAlongRangeMissing") {
                // The painted cue's range isn't in the live DOM (the reader turned away). Clear
                // the stale paint; the controller re-navigates/repaints when following resumes.
                if (!shell.readAlongAvailable) return
                paper.clearReadAlong()
            }
        }
    }

    // The reading-ruler overlay (Task 11) sits BETWEEN the paper and the chrome: over the
    // page (dims it), under the interactive chrome. PURE PAINT — no MouseArea — so it can
    // never block text selection; its props come straight from shell.appearance's ruler
    // controls, and it repositions via the Appearance panel's "Band position" slider (yPct).
    RulerOverlay {
        id: rulerOverlay
        anchors.fill: parent
        // On only when the setting is enabled, the current page is real TEXT (not a cover /
        // full-image page — a focus band over a cover is a bug), AND no overlay is up (the
        // ruler recedes behind an open search sheet / panel / selection popover / pen card).
        on: (shell.appearance ? !!shell.appearance.rulerOn : false)
            && shell.currentPageIsText
            && !chrome.anyPanelOpen && !shell.selMenuShown && !shell.dictShown && !shell.footnoteShown
        heightPx: (shell.appearance && Number.isFinite(shell.appearance.rulerHeightPx)) ? shell.appearance.rulerHeightPx : 92
        dimPct: (shell.appearance && Number.isFinite(shell.appearance.rulerDimPct)) ? shell.appearance.rulerDimPct : 42
        yPct: (shell.appearance && Number.isFinite(shell.appearance.rulerYPct)) ? shell.appearance.rulerYPct : 40
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
        shellWindowed: shell.shellWindowed

        // left-panel data (Task 8)
        tocModel: shell.bookToc
        currentTocIndex: shell.currentTocIndex
        bookmarks: shell.bookmarks
        highlights: shell.highlights

        // Audio pane data (Task 13)
        audioAttached: shell.audioAttached
        audioTitle: shell.audioTitle
        audioCover: shell.audioCover
        audioMetaLine: shell.audioMetaLine
        followOn: shell.followOn
        audioPlaying: shell.audioPlaying
        audioTimeLine: shell.audioTimeLine
        audioProgress: shell.audioProgress
        audioSpeedLabel: shell.audioSpeedLabel
        audioPlaylist: shell.audioPlaylist
        audioCurrentIndex: shell.audioCurrentIndex
        audioPosLabel: shell.audioPosLabel
        audioDurLabel: shell.audioDurLabel
        audioDurationSec: shell.audioDurationSec
        audioVolume: shell.audioVolume
        audioMuted: shell.audioMuted

        // read-along Text Sync data (Task 6) — all inert while dormant (readAlongAvailable false)
        readAlongAvailable: shell.readAlongAvailable
        bookId: shell.bookId                       // resolves the Sync-status card to this book (Task 7)
        readAlongMode: shell.readAlongMode
        readAlongWordScale: shell.readAlongWordScale
        readAlongPreviewActive: shell.readAlongPreviewActive
        readAlongPreviewLabel: shell.readAlongPreviewLabel
        readAlongFollowDetached: shell.readAlongFollowDetached

        // appearance panel data (Task 10)
        appearance: shell.appearance

        // search sheet data (Task 11)
        searchResults: shell.searchResults
        searchCount: shell.searchCount
        searchCapped: shell.searchCapped
        searchLastQuery: shell.searchLastQuery

        onBackRequested: shell.goBack()
        onMinimizeRequested: shell.goMinimize()
        onFullscreenRequested: shell.fullscreenRequested()
        onCloseRequested: shell.goBack()   // the X = same session-aware close as Back (flush + closed)
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

        // --- Audio pane (Task 13): the reader drives the shared AudiobookSession ---
        onFollowToggled: (on) => shell.setFollow(on)
        onAudioPlayToggled: shell.audioPlayToggle()
        onAudioSpeedCycled: shell.audioCycleSpeed()
        onAudioSeekRequested: (f) => shell.audioSeekFraction(f)
        onAudioSkipRequested: (s) => shell.audioSkip(s)
        onAudioPrevChapterRequested: shell.audioPrevChapter()
        onAudioNextChapterRequested: shell.audioNextChapter()
        onAudioChapterPicked: (i) => shell.audioPlayAt(i)
        onAudioVolumeRequested: (f) => shell.audioSetVolume(f)
        onAudioMuteToggled: shell.audioToggleMute()

        // --- read-along (Task 6): the aligned scrub rail + Text Sync + Return to narration ---
        // The rail is split: hover/drag PREVIEWS (no seek), release COMMITS once. dispatchScrub
        // routes to the controller when available, a plain seek when dormant.
        onAudioScrubPreviewed: (f) => shell.dispatchScrub("preview", f)
        onAudioScrubCommitted: (f) => shell.dispatchScrub("commit", f)
        onReturnToNarrationRequested: shell.returnToNarration()
        onReadAlongModePicked: (m) => shell.applyReadAlongPatch("mode", m)
        onReadAlongScaleChanged: (s) => shell.applyReadAlongPatch("wordScale", s)

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

        // Search sheet (Task 11). The icon click opens/toggles it; searchRequested fires as it
        // opens, so we reset the view-model + drop any prior search-highlight for a fresh sheet.
        onSearchRequested: { shell.resetSearch(); paper.clearSearch() }
        // Enter in the sheet → search the book; the glue caps the payload + flags `capped`,
        // and the hits arrive as the 'searchResults' paper event handled above.
        onSearchSubmitted: (q) => { if (q !== "") paper.search(q); else { shell.resetSearch(); paper.clearSearch() } }
        // A hit was clicked → jump there; the sheet STAYS OPEN so you can click through hits.
        onSearchResultActivated: (cfi) => { if (cfi !== "") paper.goTo(cfi) }
        // Appearance panel (Task 10): the chrome owns the right panel; each control edit lands
        // here → merge + persist (reader2 sub-object) + live-apply to the paper.
        onAppearanceEdited: (key, value) => shell.applyAppearancePatch(key, value)
        onAppearanceDefaultRequested: shell.applyUseAsDefault()
        onAppearanceResetRequested: shell.applyResetBook()
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

    // Failed-open surface (Part B3). A quiet centered message over the dead (black) paper when
    // the glue emits 'error' before the book became 'ready'. Declared top-most (last visible
    // item) so it covers the page; "Go back" (or Esc, when it holds focus) returns to the shelf.
    Rectangle {
        id: openErrorView
        anchors.fill: parent
        visible: shell.openErrorShown
        color: Qt.rgba(Theme.scrim.r, Theme.scrim.g, Theme.scrim.b, 0.97)
        focus: shell.openErrorShown
        onVisibleChanged: if (visible) forceActiveFocus()
        Keys.onEscapePressed: shell.goBack()
        // swallow clicks + wheel so nothing reaches the dead paper beneath (declared FIRST → the
        // Column below sits on top and still gets its button clicks).
        MouseArea { anchors.fill: parent; onWheel: (w) => { w.accepted = true } }

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 96, 440)
            spacing: 14

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Couldn't open this book"
                color: Theme.ink
                font.family: Theme.display
                font.pixelSize: 22
            }
            Text {
                width: parent.width
                visible: shell.openErrorText !== ""
                text: shell.openErrorText
                color: Theme.inkFaint
                font.family: Theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: backLabel.width + 34
                height: 38
                radius: 19
                color: Theme.bar
                border.color: Theme.barBorder
                border.width: 1
                Text {
                    id: backLabel
                    anchors.centerIn: parent
                    text: "Go back"
                    color: backMa.containsMouse ? Theme.ink : Theme.inkDim
                    font.family: Theme.ui
                    font.weight: Font.DemiBold
                    font.pixelSize: 14
                }
                MouseArea {
                    id: backMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: shell.goBack()
                }
            }
        }
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
        // ISSUE this open's generation (re-review #2 rework): QML owns the counter and hands it
        // to the glue, which echoes it on every book-scoped emit. From this line on, the only
        // 'ready' this shell will honor is one stamped with exactly this gen — a queued 'ready'
        // from a superseded slow open can never be adopted again. bookReady drops here too (not
        // only in openBook) so the glue-reload path is equally gated.
        shell.currentGen = Math.max(1, shell.currentGen + 1)
        shell.bookReady = false
        // Authorize the paper to read ONLY this book (hardening) BEFORE handing it the path —
        // the untrusted paper can then pull this book's bytes and nothing else off disk.
        Reader2Bridge.setAuthorizedBook(path)
        paper.open(path, L.resumeCfiOf(entry), shell.currentGen)
    }

    // Catalog metadata for the open book ({title, author, pairKey?…}), injected by the host
    // (Main.qml's bookReaderLayer). Only the pairing SELF-HEAL below reads it — the shell's
    // own display identity still comes from the paper's 'ready' metadata. Optional: the
    // standalone harness injects nothing and the heal simply no-ops.
    property var bookMeta: ({})

    // PAIRING SELF-HEAL (2026-07-18): the book page's auto-attach passes the reader's bookId
    // at DOWNLOAD time — if the epub's local path wasn't resolved at that exact moment, the
    // attach silently never happened, and the Audio tab stayed dead even with the audiobook
    // fully on disk (the reported miss). The reader knows its own bookId and can derive the
    // SAME title|author pairKey Biblio keys audiobooks by — so on every open: pairing absent
    // + a downloaded audiobook matching this book's pairKey → write the missing pairing right
    // here. Timing-proof; revision-driven bindings light the Audio tab immediately.
    function healAudioPairing() {
        if (typeof AudioPairing === "undefined" || typeof Audiobooks === "undefined") return
        if (shell.bookId === "") return
        var existing = AudioPairing.getPairing(shell.bookId)
        if (existing && existing.pairKey) return         // already attached — nothing to heal
        var meta = shell.bookMeta || ({})
        var pk = (meta.pairKey !== undefined && meta.pairKey !== null && String(meta.pairKey) !== "")
               ? String(meta.pairKey)
               : B.pairKey(meta.title || "", meta.author || "")
        if (pk === "" || pk === "|") return              // no usable identity → nothing to match by
        if (!Audiobooks.isDownloaded(pk)) return         // no audiobook on disk for this book
        AudioPairing.savePairing(shell.bookId, {
            "pairKey": pk,
            "dirPath": Audiobooks.localAudiobook(pk)
        })
        if (shell.readerDebug) console.log("[shell] healed audio pairing:", shell.bookId, "→", pk)
    }

    function openBook(path) {
        shell.flushProgressSave()          // don't lose the previous book's last position (Part B4)
        shell.bookReady = false            // the new book hasn't reached 'ready' yet (Part B3 gate)
        shell.openErrorShown = false       // drop any prior failed-open surface
        shell.openErrorText = ""
        bookPath = path
        shell.healAudioPairing()           // repair a missed auto-attach before the panel binds
        if (paper.glueUp) shell.openAtResume(path)
    }

    function openAudioPanel() {
        chrome.openPanelTo("audio")
    }
}
