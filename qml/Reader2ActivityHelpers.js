.pragma library

// Reader2ActivityHelpers.js — pure reading-activity decision logic for Lane D (Biblio
// Reader 2), CPP-PORT-CONTRACT.md §7 identity, §9 Lane D, §22 Reader-2 proofs. No QML types,
// no engine, no Date/network — data-in / data-out, exactly the ActivityLaneHelpers.js /
// Player2ActivityHelpers.js precedent for Lanes A/B/E, so tests/qml/tst_reader2_activity.qml
// exercises the SAME code qml/reader2/ReaderShell.qml's 'relocated' handler actually runs.
//
// THE CENTRAL LAW (renderer page numbers are NOT truth — AUDIT.md Lane 4 #9, CPP-PORT-
// CONTRACT.md §9 Lane D "Reflowable content"): resources/reader2/paper_glue.js's
// `pageInChapter`/`pagesInChapter` are foliate's RENDERER-DERIVED pagination for a reflowable
// book — they shift with font size, margins, flow, and viewport. They must NEVER become
// "Pages read." The only durable reflowable engagement unit is a FORWARD delta of the
// whole-book `fraction` (canonical CFI-based progress), and only while the relocation that
// produced it is both ACCEPTED (current generation, book ready — ReaderShell's existing
// L.acceptBookEvent gate, untouched) and carries cause "sequential".
//
// PROVENANCE (`cause`) — threaded by paper_glue.js onto every 'relocated' emit, BEFORE the
// 60ms persistence debounce:
//   "programmatic" — the glue's own `programmaticNav` flag: the initial/resume open (view.init
//                    is wrapped in beginProgrammatic/endProgrammatic) AND every read-along
//                    controller move (navigateReadAlong/ensureReadAlongVisible, same wrap).
//                    Foliate has no separate "this was the FIRST relocate after ready" signal,
//                    but initial/resume needs no separate bucket: it already reaches here as
//                    "programmatic" and this module treats every non-"sequential" cause
//                    identically (zero, reset baseline) — see decideReadingActivity below.
//   "jump"         — the glue's own `jumpNav` flag, set only inside paperGoTo(): the ONE
//                    function TOC/search/bookmark/scrub-rail jumps all funnel through
//                    (ReaderShell's onTocActivated/onBookmarkActivated/onHighlightActivated/
//                    onSearchResultActivated/onScrubbed/onReturnRequested all call paper.goTo).
//   "layout"       — the glue's own bounded `layoutNavUntilMs` window, armed inside
//                    paperSetAppearance(): a font/margin/theme/flow/column edit reflows the
//                    page and foliate re-anchors to (approximately) the SAME position. Best-
//                    effort/bounded, not depth-counted — see paper_glue.js's own comment for
//                    why a missed tag here is safe, not an overcount vector.
//   "sequential"   — the default: an ordinary page turn (next()/prev(), arrow keys, HUD
//                    buttons) or a live scroll settle. The ONLY cause that may ever credit
//                    reading progress or complete the book.
//   A relocation whose generation is stale, or that belongs to a book already superseded by a
//   newer open, never reaches this module at all — ReaderShell's existing L.acceptBookEvent
//   gate (untouched by this slice) drops it before the 'relocated' handler does anything.
//
// FIXED-LAYOUT (PROVEN, not assumed): CBZ and PDF books opened through Reader2 (Biblio's ONE
// generic book opener — qml/Main.qml's bookReaderLayer always sources reader2/ReaderShell.qml
// regardless of format; this is NOT the separate Tankoban comic lane) set foliate's own
// `view.isFixedLayout` true. For those, `chapterLocation.current`/`.total` (this module's
// `pageInChapter`/`pagesInChapter`) are NOT renderer pagination — they come from
// SectionProgress, which is built from `book.sections` (one section per CBZ page / PDF page,
// fixed at parse time — see vendor/foliate-anx/src/{comic-book,pdf,progress,fixed-layout}.js)
// and is completely independent of font/margin/viewport: paper_glue.js's applyAppearance()
// returns immediately for a fixed-layout book, applying only the background color, before any
// reflow knob is touched. That is real, source-grounded proof of a stable physical page
// identity, so a fixed-layout Reader2 book DOES qualify for readingForm:"fixed" with a real
// pageKey. Every OTHER Reader2 format (EPUB/MOBI/FB2/TXT) exposes no such proof and stays
// reflowable — this module treats "not fixed-layout" as the conservative default, per §9 Lane D
// "if that proof is absent, treat conservatively as reflowable."

// ---------------------------------------------------------------------------
// §7 identity — Biblio ebook.
// ---------------------------------------------------------------------------

// biblioIdentityFor(bookMeta, bookId) → { kind, titleKey, itemKey, syncable } | null.
// Prefers a real catalog metadata id (bookMeta.id) when present; falls back to the reader's
// own opaque local key (bookId — Reader2Bridge.bookKey(path), a SHA1[:20] fingerprint, NEVER
// the raw path) and marks that fallback syncable:false (§7: "use an opaque local key... set
// syncable:false... do not claim cross-device identity"). Returns null only when NEITHER a
// metadata id nor a bookId is available — §25 fail-closed: no stable identity, no activity.
function biblioIdentityFor(bookMeta, bookId) {
    var m = bookMeta || {}
    var metaId = (m.id !== undefined && m.id !== null && String(m.id).length) ? String(m.id) : ""
    var localKey = (bookId !== undefined && bookId !== null) ? String(bookId) : ""
    var itemKey = metaId.length ? metaId : localKey
    if (!itemKey.length)
        return null
    return {
        kind: "book",
        titleKey: "biblio:" + itemKey,
        itemKey: itemKey,
        syncable: metaId.length > 0
    }
}

// ---------------------------------------------------------------------------
// §9 Lane D — the reflow/fixed reading-progress decision (pure).
// ---------------------------------------------------------------------------

var MICROS_PER_UNIT = 1000000

// decideReadingActivity(baselineFraction, relocation) → the reading-delta half of the
// decision, independent of completion.
//   baselineFraction : the last SEEDED forward-reading fraction, or null/undefined when there
//                       is none yet (fresh book open, or the immediately-prior relocation reset
//                       it — a jump/programmatic/layout cause, or a backward move).
//   relocation        : { cause, fraction, isFixedLayout, pageInChapter }
//     cause         : "sequential" | "jump" | "programmatic" | "layout" (anything else is
//                     treated exactly like "jump"/"programmatic" — fail closed, never credited).
//     fraction      : the accepted relocation's whole-book fraction (0..1).
//     isFixedLayout : true only for a proven fixed-layout Reader2 book (CBZ/PDF).
//     pageInChapter : for a fixed-layout book, the 0-based section/page index (see the header
//                     note) — ignored for a reflowable book.
// Returns { emit, readingForm, pageKeys, progressMicros, baselineFraction }:
//   emit             : should the caller call ProfileActivity.recordReadingDelta()? A
//                       reading_delta with empty pageKeys AND progressMicros===0 is REJECTED as
//                       malformed by ActivityProjector::validateEvent() ("empty reading_delta"),
//                       so a "zero" cause never becomes a fact — it only resets the baseline.
//   readingForm      : "fixed" | "reflowable", from relocation.isFixedLayout — reported even on
//                       a no-emit result so a caller that wants to log/inspect it consistently
//                       can, though it is meaningless without emit.
//   pageKeys         : ["page:<index>"] for an emitted fixed-layout delta, else [] always
//                       (reflowable pageKeys MUST be empty — enforced natively too).
//   progressMicros   : round(deltaFraction * 1e6) for an emitted delta, else 0.
//   baselineFraction : the fraction the CALLER should store for the NEXT relocation. Every
//                       non-"sequential" cause and every backward/non-positive move resets it
//                       to the CURRENT fraction (§9 "reset forward baseline"); a "sequential"
//                       cause with no prior baseline SEEDS it (this relocation itself never
//                       emits — there is no earlier position to diff against).
function decideReadingActivity(baselineFraction, relocation) {
    var r = relocation || {}
    var fixed = !!r.isFixedLayout
    var readingForm = fixed ? "fixed" : "reflowable"
    var fraction = Number.isFinite(r.fraction) ? r.fraction : 0
    var haveBaseline = Number.isFinite(baselineFraction)

    var reset = function () {
        return { emit: false, readingForm: readingForm, pageKeys: [], progressMicros: 0, baselineFraction: fraction }
    }

    if (r.cause !== "sequential")
        return reset()               // jump / programmatic / layout / unrecognized — fail closed
    if (!haveBaseline)
        return reset()                // nothing to diff against yet — seed only
    var delta = fraction - baselineFraction
    if (!(delta > 0))
        return reset()                // backward or unchanged — zero + reset (§9)

    var micros = Math.round(delta * MICROS_PER_UNIT)
    if (micros <= 0)
        return reset()                // sub-resolution forward jitter — nothing worth recording

    var pageKeys = []
    if (fixed && Number.isFinite(r.pageInChapter) && r.pageInChapter >= 0)
        pageKeys = ["page:" + r.pageInChapter]

    return { emit: true, readingForm: readingForm, pageKeys: pageKeys, progressMicros: micros, baselineFraction: fraction }
}

// decideCompletion(cause, percent, alreadyCompleted) → should THIS relocation record
// media_completed{reason:"sequential_book_end"}? Only an ACCEPTED SEQUENTIAL relocation that
// reaches the existing rounded-100% condition (mirrors Reader2Logic.progressRecord's
// `percent >= 100`) completes the book; a jump/programmatic/layout relocation that happens to
// land at 100% (a direct jump to the end) never does (§9 Lane D). `alreadyCompleted` is a
// level-trigger latch the caller carries per book-open session so a repeated forward nudge
// while already at the end doesn't keep re-emitting the same completion fact.
function decideCompletion(cause, percent, alreadyCompleted) {
    if (alreadyCompleted)
        return false
    return cause === "sequential" && Number.isFinite(percent) && percent >= 100
}

// ---------------------------------------------------------------------------
// The one entry point ReaderShell.qml's 'relocated' handler calls.
// ---------------------------------------------------------------------------

// activityDecision(state, relocation) → the full decision for one accepted relocation.
//   state      : { baselineFraction, completed } — the caller's per-book-open activity
//                bookkeeping (reset to { baselineFraction: null, completed: false } on every
//                fresh book open / new activity session — a book switch or stale-generation
//                relocation never reaches here at all, since ReaderShell's existing
//                L.acceptBookEvent gate drops it first).
//   relocation : { cause, fraction, percent, isFixedLayout, pageInChapter } — see
//                decideReadingActivity's header for each field.
// Returns { emitReading, readingForm, pageKeys, progressMicros, emitCompletion, newState }.
function activityDecision(state, relocation) {
    var st = state || {}
    var r = relocation || {}
    var rd = decideReadingActivity(st.baselineFraction, r)
    var completes = decideCompletion(r.cause, r.percent, !!st.completed)
    return {
        emitReading: rd.emit,
        readingForm: rd.readingForm,
        pageKeys: rd.pageKeys,
        progressMicros: rd.progressMicros,
        emitCompletion: completes,
        newState: { baselineFraction: rd.baselineFraction, completed: !!st.completed || completes }
    }
}

// freshState() → the activity bookkeeping to install on a fresh book open (no prior baseline,
// not completed). A small named constructor so ReaderShell doesn't hand-roll the shape.
function freshState() {
    return { baselineFraction: null, completed: false }
}
