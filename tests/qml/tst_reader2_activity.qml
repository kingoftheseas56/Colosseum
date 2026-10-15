import QtQuick 2.15
import QtTest 1.3
import "../../qml/Reader2ActivityHelpers.js" as AH

// Slice D8 — Lane D (Biblio Reader 2) reading-activity hook regression,
// CPP-PORT-CONTRACT.md §7 identity, §9 Lane D, §22 Reader-2 proofs.
//
// qml/reader2/ReaderShell.qml itself is not loaded by the generic QuickTest runner: it needs
// the WebEngine-backed Paper component plus Colosseum.Activity/ProfileActivity, which (like
// Colosseum.Player in tst_player1_activity.qml's header) are C++ types registered only inside
// native/main.cpp's qmlRegisterType() calls, not through a qmldir the offscreen runner can
// resolve. So this suite tests the exact PURE decision logic ReaderShell's 'relocated' handler
// is built on — qml/Reader2ActivityHelpers.js — directly, plus a small local mirror of
// ReaderShell.recordReadingActivity()'s wiring (identity → AH.activityDecision → fact shaping)
// driven by a RECORDING FAKE ProfileActivity, so the emit/no-emit/completion routing is proven
// independent of the real store.
TestCase {
    name: "Reader2Activity"

    // ---- §7 identity derivation (biblioIdentityFor) --------------------------------------

    function test_metadata_id_yields_syncable_identity() {
        var idf = AH.biblioIdentityFor({ "id": "gcd:12345", "title": "Dune" }, "localkey-abc")
        verify(idf !== null)
        compare(idf.kind, "book")
        compare(idf.titleKey, "biblio:gcd:12345")
        compare(idf.itemKey, "gcd:12345")
        compare(idf.syncable, true)
    }

    function test_path_derived_fallback_is_never_syncable() {
        // §7: "use an opaque local key, never the raw path... set syncable:false." bookId here
        // stands in for Reader2Bridge.bookKey(path) — a SHA1[:20] fingerprint, not a raw path.
        var idf = AH.biblioIdentityFor({}, "sha1fingerprint20chars")
        verify(idf !== null)
        compare(idf.itemKey, "sha1fingerprint20chars")
        compare(idf.titleKey, "biblio:sha1fingerprint20chars")
        compare(idf.syncable, false)
    }

    function test_no_identity_available_fails_closed_to_null() {
        // §25 fail-closed: neither a metadata id nor a local bookId → no stable identity, ever.
        verify(AH.biblioIdentityFor({}, "") === null)
        verify(AH.biblioIdentityFor(null, undefined) === null)
    }

    // ---- §9 Lane D reading-delta decision (decideReadingActivity) ------------------------

    function test_initial_resume_relocate_adds_zero() {
        // The first relocate after 'ready' always carries cause "programmatic" (paper_glue.js
        // wraps view.init() in its programmatic tag).
        var rd = AH.decideReadingActivity(null, { "cause": "programmatic", "fraction": 0.02, "isFixedLayout": false })
        compare(rd.emit, false)
        compare(rd.baselineFraction, 0.02)   // baseline resets to the resumed position
    }

    function test_sequential_forward_reading_credits_progress_with_zero_pages() {
        // First sequential relocate only SEEDS the baseline (nothing to diff against yet).
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.10, "isFixedLayout": false })
        compare(seed.emit, false)
        compare(seed.baselineFraction, 0.10)
        // A later sequential forward relocate credits the delta.
        var rd = AH.decideReadingActivity(seed.baselineFraction, { "cause": "sequential", "fraction": 0.15, "isFixedLayout": false })
        compare(rd.emit, true)
        compare(rd.readingForm, "reflowable")
        compare(rd.pageKeys.length, 0)          // §9: reflowable pageKeys MUST be empty
        compare(rd.progressMicros, 50000)       // round(0.05 * 1e6)
        compare(rd.baselineFraction, 0.15)
    }

    function test_renderer_page_numbers_never_drive_reflowable_progress() {
        // Regression for the central law (AUDIT.md Lane 4 #9): pageInChapter/pagesInChapter
        // are never read for a reflowable delta — only the whole-book fraction is. A huge
        // pageInChapter alongside a tiny real fraction delta must still compute from fraction.
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.5, "isFixedLayout": false, "pageInChapter": 400, "pagesInChapter": 401 })
        var rd = AH.decideReadingActivity(seed.baselineFraction,
            { "cause": "sequential", "fraction": 0.500001, "isFixedLayout": false, "pageInChapter": 1, "pagesInChapter": 2 })
        compare(rd.pageKeys.length, 0)
        compare(rd.progressMicros, 1)   // round(0.000001 * 1e6) — driven by fraction, not pageInChapter
    }

    function test_font_viewport_layout_relocation_adds_zero() {
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.30, "isFixedLayout": false })
        // The reflow re-anchor lands slightly FORWARD of the pre-edit position (a real anchor
        // recompute is rarely bit-exact) — this must still credit nothing: cause "layout" is
        // gated before any fraction comparison happens.
        var rd = AH.decideReadingActivity(seed.baselineFraction, { "cause": "layout", "fraction": 0.31, "isFixedLayout": false })
        compare(rd.emit, false)
        compare(rd.baselineFraction, 0.31)   // resets to the new (post-reflow) position
    }

    function test_toc_search_bookmark_jump_adds_zero_for_skipped_material() {
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.10, "isFixedLayout": false })
        // Jump far ahead (skips material) — must NOT credit the skipped span.
        var rd = AH.decideReadingActivity(seed.baselineFraction, { "cause": "jump", "fraction": 0.80, "isFixedLayout": false })
        compare(rd.emit, false)
        compare(rd.baselineFraction, 0.80)
        // The reset baseline means the NEXT relocation diffs from 0.80, not the pre-jump 0.10.
        var after = AH.decideReadingActivity(rd.baselineFraction, { "cause": "sequential", "fraction": 0.81, "isFixedLayout": false })
        compare(after.emit, true)
        compare(after.progressMicros, 10000)   // round(0.01 * 1e6), NOT round(0.71 * 1e6)
    }

    function test_backward_reread_adds_zero_and_resets_baseline() {
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.40, "isFixedLayout": false })
        var rd = AH.decideReadingActivity(seed.baselineFraction, { "cause": "sequential", "fraction": 0.35, "isFixedLayout": false })
        compare(rd.emit, false)
        compare(rd.baselineFraction, 0.35)
        // Subsequent genuine forward reading from the NEW location emits again.
        var after = AH.decideReadingActivity(rd.baselineFraction, { "cause": "sequential", "fraction": 0.37, "isFixedLayout": false })
        compare(after.emit, true)
        compare(after.progressMicros, 20000)
    }

    function test_no_baseline_change_is_a_noop_not_a_credit() {
        // An exact-equal relocation (delta === 0) must not emit (would also fail native
        // validation as an empty reading_delta if it somehow did).
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.5, "isFixedLayout": false })
        var rd = AH.decideReadingActivity(seed.baselineFraction, { "cause": "sequential", "fraction": 0.5, "isFixedLayout": false })
        compare(rd.emit, false)
    }

    // ---- fixed-layout (CBZ/PDF via Reader2) proven physical-page identity -----------------

    function test_fixed_layout_sequential_forward_emits_a_stable_page_key() {
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.10, "isFixedLayout": true, "pageInChapter": 3 })
        var rd = AH.decideReadingActivity(seed.baselineFraction,
            { "cause": "sequential", "fraction": 0.12, "isFixedLayout": true, "pageInChapter": 4 })
        compare(rd.emit, true)
        compare(rd.readingForm, "fixed")
        compare(rd.pageKeys.length, 1)
        compare(rd.pageKeys[0], "page:4")
        compare(rd.progressMicros, 20000)
    }

    function test_fixed_layout_jump_adds_zero_same_as_reflowable() {
        var seed = AH.decideReadingActivity(null, { "cause": "sequential", "fraction": 0.10, "isFixedLayout": true, "pageInChapter": 1 })
        var rd = AH.decideReadingActivity(seed.baselineFraction,
            { "cause": "jump", "fraction": 0.90, "isFixedLayout": true, "pageInChapter": 40 })
        compare(rd.emit, false)
        compare(rd.pageKeys.length, 0)
    }

    // ---- §9 completion (decideCompletion / activityDecision) ------------------------------

    function test_sequential_relocation_reaching_100_percent_completes() {
        var state = AH.freshState()
        var seed = AH.activityDecision(state, { "cause": "sequential", "fraction": 0.99, "percent": 99, "isFixedLayout": false })
        var end = AH.activityDecision(seed.newState, { "cause": "sequential", "fraction": 1.0, "percent": 100, "isFixedLayout": false })
        compare(end.emitCompletion, true)
        compare(end.newState.completed, true)
    }

    function test_jump_to_end_never_completes() {
        var state = AH.freshState()
        var jumped = AH.activityDecision(state, { "cause": "jump", "fraction": 1.0, "percent": 100, "isFixedLayout": false })
        compare(jumped.emitCompletion, false)
        compare(jumped.newState.completed, false)
    }

    function test_programmatic_relocation_at_end_never_completes() {
        // Covers the initial/resume-at-the-last-page case (cause "programmatic").
        var state = AH.freshState()
        var initial = AH.activityDecision(state, { "cause": "programmatic", "fraction": 1.0, "percent": 100, "isFixedLayout": false })
        compare(initial.emitCompletion, false)
    }

    function test_completion_does_not_re_fire_while_already_completed() {
        var state = AH.freshState()
        var first = AH.activityDecision(state, { "cause": "sequential", "fraction": 1.0, "percent": 100, "isFixedLayout": false })
        compare(first.emitCompletion, true)
        // A further forward nudge while still at 100% (e.g. re-reading the last page) must not
        // spam a second completion fact for the same book-open session.
        var again = AH.activityDecision(first.newState, { "cause": "sequential", "fraction": 1.0, "percent": 100, "isFixedLayout": false })
        compare(again.emitCompletion, false)
    }

    // ---- wiring mirror: recordReadingActivity(p) against a recording fake ProfileActivity --
    // Mirrors qml/reader2/ReaderShell.qml's recordReadingActivity() exactly (same AH calls,
    // same activityState bookkeeping, same base-fact shape) but against a plain QtObject fake
    // instead of the real C++ ActivityStore, so the ROUTING (does recordReadingDelta/
    // recordCompletion fire, with what payload) is provable offscreen. staleGate mirrors
    // ReaderShell's existing (untouched) L.acceptBookEvent gate: a stale-generation or pre-
    // ready event never reaches recordReadingActivity at all.
    QtObject {
        id: fakeSink
        property var readingCalls: []
        property var completionCalls: []
        function newSessionId() { return "sess-1" }
        function recordReadingDelta(fact) { readingCalls.push(fact) }
        function recordCompletion(fact) { completionCalls.push(fact) }
    }
    QtObject {
        id: lane
        property var activityState: AH.freshState()
        property string activitySessionId: "sess-1"
        property var bookMeta: ({ "id": "gcd:999" })
        property string bookId: "localkey"
        property string bookTitle: "Test Book"
        property string chapterLabel: "Chapter 1"

        function recordReadingActivity(p, gen, currentGen, bookReady) {
            // The staleness/readiness gate ReaderShell applies BEFORE ever calling
            // recordReadingActivity — untouched by this slice, mirrored here only so the test
            // can prove a stale/pre-ready event never reaches the activity code at all.
            if (!bookReady || (gen !== undefined && gen < currentGen))
                return

            var idf = AH.biblioIdentityFor(lane.bookMeta, lane.bookId)
            if (!idf) return

            var relocation = { "cause": p.cause, "fraction": p.fraction, "percent": p.percent,
                                "isFixedLayout": !!p.isFixedLayout, "pageInChapter": p.pageInChapter }
            var decision = AH.activityDecision(lane.activityState, relocation)
            lane.activityState = decision.newState

            var base = { "sessionId": lane.activitySessionId, "world": "biblio", "kind": idf.kind,
                          "titleKey": idf.titleKey, "itemKey": idf.itemKey, "title": lane.bookTitle,
                          "itemLabel": lane.chapterLabel, "cover": "", "utcOffsetMinutes": 330,
                          "syncable": idf.syncable, "source": "reader2" }

            if (decision.emitReading) {
                var rf = {}
                for (var rk in base) rf[rk] = base[rk]
                rf.atMs = 1000
                rf.readingForm = decision.readingForm
                rf.pageKeys = decision.pageKeys
                rf.progressMicros = decision.progressMicros
                fakeSink.recordReadingDelta(rf)
            }
            if (decision.emitCompletion) {
                var cf = {}
                for (var ck in base) cf[ck] = base[ck]
                cf.atMs = 1000
                cf.reason = "sequential_book_end"
                fakeSink.recordCompletion(cf)
            }
        }
    }

    function init() {
        fakeSink.readingCalls = []
        fakeSink.completionCalls = []
        lane.activityState = AH.freshState()
    }

    function test_stale_generation_relocate_never_reaches_activity_code() {
        // gen 1 belongs to a superseded open; currentGen is already 2 — ReaderShell's existing
        // gate drops this before recordReadingActivity does anything (§9 "book switch/stale
        // generation = discard").
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 0.5, "percent": 50 }, 1, 2, true)
        compare(fakeSink.readingCalls.length, 0)
        compare(fakeSink.completionCalls.length, 0)
    }

    function test_pre_ready_relocate_never_reaches_activity_code() {
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 0.5, "percent": 50 }, 1, 1, false)
        compare(fakeSink.readingCalls.length, 0)
    }

    function test_accepted_sequential_forward_relocation_records_a_reading_delta() {
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 0.10, "percent": 10 }, 1, 1, true)
        compare(fakeSink.readingCalls.length, 0)   // first sequential relocate only seeds
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 0.20, "percent": 20 }, 1, 1, true)
        compare(fakeSink.readingCalls.length, 1)
        var fact = fakeSink.readingCalls[0]
        compare(fact.world, "biblio")
        compare(fact.kind, "book")
        compare(fact.titleKey, "biblio:gcd:999")
        compare(fact.readingForm, "reflowable")
        compare(fact.pageKeys.length, 0)
        compare(fact.progressMicros, 100000)
        compare(fact.syncable, true)
    }

    function test_end_to_end_sequential_completion_records_completion_fact() {
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 0.95, "percent": 95 }, 1, 1, true)
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 1.0, "percent": 100 }, 1, 1, true)
        compare(fakeSink.completionCalls.length, 1)
        compare(fakeSink.completionCalls[0].reason, "sequential_book_end")
    }

    function test_end_to_end_jump_to_end_records_no_completion() {
        lane.recordReadingActivity({ "cause": "sequential", "fraction": 0.10, "percent": 10 }, 1, 1, true)
        lane.recordReadingActivity({ "cause": "jump", "fraction": 1.0, "percent": 100 }, 1, 1, true)
        compare(fakeSink.completionCalls.length, 0)
        compare(fakeSink.readingCalls.length, 0)   // the jump itself never credits reading either
    }
}
