import QtQuick
import QtTest 1.3
import "../../qml/ComicActivityHelpers.js" as ComicActivityHelpers

// Slice D7 — Lane C (Tankoban/manga/comics) activity hook regression, CPP-PORT-CONTRACT.md
// §7/§9/§10, AUDIT.md Lane 3.
//
// Two layers, chosen for what each can actually prove offscreen:
//
//   1. qml/comicreader/ComicReaderShell.qml itself DOES load and run offscreen — it imports no
//      C++-registered QML type (unlike qml/PlayerPage.qml / qml/player2/Player2Shell.qml, which
//      tests/qml/tst_player1_activity.qml / tst_player2_activity.qml document as unloadable for
//      exactly that reason). tests/qml/tst_comicreader_resume_race.qml already proves this by
//      instantiating the real shell via Qt.createComponent + createTemporaryObject with `core`
//      left null and fake progress/pageStore seams. This suite reuses that exact pattern to
//      drive the shell's REAL _activityBeginIfNeeded()/_onActivityPagesPresented()/
//      _checkActivityCoverage() functions against a recording fake ProfileActivity — the
//      session/identity/fact-shape/coverage-completion wiring is proven against production
//      code, not a reimplementation.
//
//   2. The three reading surfaces (ComicReaderSingleSurface/DoubleSurface/StripSurface.qml)
//      gate their activityPagesPresented signal on real Image.status reaching Ready, which only
//      happens through a genuine decode. Reaching that offscreen needs a full fake
//      ComicReaderCore + real fixture image files (the ~2000-line setup
//      tests/comicreader_surfaces_harness.qml already carries for the pre-existing presented()
//      suite) — disproportionate for three new signals. So, mirroring the estabished house
//      pattern for a lane whose production file cannot be driven offscreen (Player 1/Player 2/
//      audiobook's ActivityLaneHelpers-based "local mirror of the hook wiring" — see those
//      three tst_*_activity.qml headers), each surface's exact activityPagesPresented gating
//      predicate (named in the production _noteActivityPresented()/_noteActivityCentre()
//      comments) is reproduced here as a small local QtObject mirror and exercised directly, so
//      a change to any rule below without an equal change to the production file fails here.
TestCase {
    id: testCase
    name: "ComicActivity"

    // ============================================================================
    // Layer 0 — ComicActivityHelpers.js pure functions
    // ============================================================================

    function test_activityKindFor_maps_all_three_progress_kinds() {
        compare(ComicActivityHelpers.activityKindFor("manga"), "manga_chapter")
        compare(ComicActivityHelpers.activityKindFor("comic"), "comic_issue")
        compare(ComicActivityHelpers.activityKindFor("tankoban"), "tankoban_volume")
    }

    function test_activityKindFor_unknown_fails_closed_to_null() {
        // §25 fail-closed: an unrecognised progressKind must never invent a kind.
        verify(ComicActivityHelpers.activityKindFor("bogus") === null)
        verify(ComicActivityHelpers.activityKindFor("") === null)
        verify(ComicActivityHelpers.activityKindFor(undefined) === null)
    }

    function test_identityFor_uses_the_tankoban_namespace_for_every_lane() {
        // §7: "Manga/comic/Tankoban: titleKey = tankoban:<seriesId>" — the SAME namespace
        // whether the entry is a manga chapter, a western comic issue or a Tankoban volume;
        // only `kind` tells the three apart.
        var manga = ComicActivityHelpers.identityFor("series-x", "ch1", "manga")
        var comic = ComicActivityHelpers.identityFor("series-x", "issue1", "comic")
        var tank  = ComicActivityHelpers.identityFor("series-x", "vol1", "tankoban")
        compare(manga.titleKey, "tankoban:series-x")
        compare(comic.titleKey, "tankoban:series-x")
        compare(tank.titleKey, "tankoban:series-x")
        compare(manga.kind, "manga_chapter")
        compare(comic.kind, "comic_issue")
        compare(tank.kind, "tankoban_volume")
        compare(manga.itemKey, "ch1")
    }

    function test_identityFor_fails_closed_on_missing_series_entry_or_kind() {
        verify(ComicActivityHelpers.identityFor("", "ch1", "manga") === null)
        verify(ComicActivityHelpers.identityFor("series-x", "", "manga") === null)
        verify(ComicActivityHelpers.identityFor("series-x", "ch1", "bogus") === null)
        verify(ComicActivityHelpers.identityFor(undefined, undefined, "manga") === null)
    }

    function test_portableCover_allows_http_https_data_only() {
        compare(ComicActivityHelpers.portableCover("https://example.com/cover.jpg"),
                "https://example.com/cover.jpg")
        compare(ComicActivityHelpers.portableCover("http://example.com/c.png"),
                "http://example.com/c.png")
        compare(ComicActivityHelpers.portableCover("data:image/png;base64,aaa"),
                "data:image/png;base64,aaa")
    }

    function test_portableCover_rejects_local_paths_to_empty() {
        // §15: "A cover field is either empty or a portable safe locator. Local file:/qrc:/
        // absolute/UNC/relative filesystem/resource paths are not portable activity metadata."
        compare(ComicActivityHelpers.portableCover("file:///C:/covers/x.png"), "")
        compare(ComicActivityHelpers.portableCover("qrc:/covers/x.png"), "")
        compare(ComicActivityHelpers.portableCover("C:/covers/x.png"), "")
        compare(ComicActivityHelpers.portableCover("\\\\server\\share\\x.png"), "")
        compare(ComicActivityHelpers.portableCover("covers/x.png"), "")
        compare(ComicActivityHelpers.portableCover(""), "")
        compare(ComicActivityHelpers.portableCover(undefined), "")
    }

    function test_pageKeysFor_shapes_dedupes_and_drops_negative_indices() {
        compare(ComicActivityHelpers.pageKeysFor([0, 2, 2, 5]), ["p0", "p2", "p5"])
        // exact dedupe: a repeated index in ONE call contributes ONE key
        compare(ComicActivityHelpers.pageKeysFor([3, 3]), ["p3"])
        // negative/non-numeric are dropped, never turned into "p-1"
        compare(ComicActivityHelpers.pageKeysFor([-1, 4, "nope"]), ["p4"])
        compare(ComicActivityHelpers.pageKeysFor([]), [])
        compare(ComicActivityHelpers.pageKeysFor(null), [])
    }

    function test_requiredPageKeys_excludes_broken_indices() {
        // §9 "the entry's exact required non-terminal-broken page set"
        compare(ComicActivityHelpers.requiredPageKeys(4, []), ["p0", "p1", "p2", "p3"])
        compare(ComicActivityHelpers.requiredPageKeys(4, [1]), ["p0", "p2", "p3"])
        compare(ComicActivityHelpers.requiredPageKeys(0, []), [])
    }

    // ============================================================================
    // Layer 1 — the REAL ComicReaderShell.qml, driven directly (mirrors
    // tests/qml/tst_comicreader_resume_race.qml's own createTemporaryObject pattern)
    // ============================================================================

    component FakeActivity: QtObject {
        property var readingDeltas: []
        property var completions: []
        property int sessionCounter: 0
        function newSessionId() { sessionCounter += 1; return "fake-session-" + sessionCounter }
        function recordReadingDelta(fact) { readingDeltas.push(fact); return true }
        function recordCompletion(fact) { completions.push(fact); return true }
        // Mirrors the real ActivityStore::hasFixedCoverage's own scoping (kind + itemKey,
        // across the FULL recorded ledger — not session-scoped) closely enough to drive the
        // shell's completion check honestly.
        function hasFixedCoverage(kind, itemKey, requiredPageKeys) {
            if (!requiredPageKeys || requiredPageKeys.length === 0) return true
            var covered = ({})
            for (var i = 0; i < readingDeltas.length; i++) {
                var f = readingDeltas[i]
                if (f.kind !== kind || f.itemKey !== itemKey) continue
                var keys = f.pageKeys || []
                for (var j = 0; j < keys.length; j++) covered[keys[j]] = true
            }
            for (var k = 0; k < requiredPageKeys.length; k++)
                if (!covered[requiredPageKeys[k]]) return false
            return true
        }
    }
    component FakePageStore: QtObject {
        property var pages: []
        function localPages(cid) { return pages }
        function downloadChapter() {}
        function downloadIssue() {}
        signal progress(string cid, real done, real total)
        signal finished(string cid)
        signal failed(string cid, string reason)
    }
    component SeriesRecords: QtObject { property string all: "{}" }

    Component { id: activityComp; FakeActivity {} }
    Component { id: storeComp; FakePageStore {} }
    Component { id: recordsComp; SeriesRecords {} }
    property var shellComp: null

    function threePages() {
        var out = []
        for (var i = 0; i < 3; i++) out.push({ index: i, url: "file:///f/p" + i + ".png", group: -1 })
        return out
    }

    function makeStore(count) {
        var s = createTemporaryObject(storeComp, testCase)
        var out = []
        for (var i = 0; i < count; i++) out.push({ index: i, url: "file:///f/p" + i + ".png", group: -1 })
        s.pages = out
        return s
    }

    function makeShell(cfg) {
        var full = Object.assign({
            "width": 640, "height": 480, "recordDebounceMs": 20,
            "entryKind": "manga", "western": false,
            "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
            "chapterId": "ch1", "chapterLabel": "Chapter 1",
            "seriesId": "series-x", "seriesTitle": "Series X",
            "seriesCover": "https://example.com/cover.jpg"
        }, cfg)
        var inst = createTemporaryObject(shellComp, testCase, full)
        verify(inst !== null, "shell createTemporaryObject must succeed")
        return inst
    }

    function initTestCase() {
        shellComp = Qt.createComponent("../../qml/comicreader/ComicReaderShell.qml")
        verify(shellComp.status !== Component.Error,
               "shell component must load: " + shellComp.errorString())
    }

    // ---- session lifecycle (§9 "fresh open or crossing" vs the hide/reshow-same-entry noop) --

    function test_session_begins_on_fresh_open() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3) })
        verify(shell.activityActiveKey.length > 0, "a fresh open must begin a session")
        verify(shell.activitySessionId.length > 0, "a session id must be minted")
    }

    function test_hide_reshow_same_entry_keeps_the_same_session() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3) })
        var before = shell.activitySessionId
        shell.visible = false
        shell.visible = true
        compare(shell.activitySessionId, before,
                "hiding/reshowing the SAME entry must not fragment the reading session")
    }

    function test_crossing_to_a_different_entry_begins_a_new_session() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3) })
        var before = shell.activitySessionId
        shell.openEntryById("ch2", false)
        verify(shell.activitySessionId.length > 0)
        verify(shell.activitySessionId !== before,
               "crossing to a different chapter/issue/volume must start a NEW session")
    }

    // ---- fact shape (§6/§7) -----------------------------------------------------------------

    function test_reading_delta_fact_shape() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3) })
        shell._onActivityPagesPresented([0])
        compare(activity.readingDeltas.length, 1)
        var fact = activity.readingDeltas[0]
        compare(fact.world, "tankoban")
        compare(fact.kind, "manga_chapter")
        compare(fact.titleKey, "tankoban:series-x")
        compare(fact.itemKey, "ch1")
        compare(fact.title, "Series X")
        compare(fact.cover, "https://example.com/cover.jpg")
        compare(fact.syncable, true)
        compare(fact.source, "comicreader-shell")
        compare(fact.sessionId, shell.activitySessionId)
        compare(fact.readingForm, "fixed")
        compare(fact.pageKeys, ["p0"])
        compare(fact.progressMicros, 0)
        compare(fact.utcOffsetMinutes, -(new Date().getTimezoneOffset()))
        verify(typeof fact.atMs === "number" && fact.atMs > 0)
    }

    function test_local_cover_is_sanitized_to_empty_in_the_recorded_fact() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3),
                                "seriesCover": "file:///C:/covers/local.png" })
        shell._onActivityPagesPresented([0])
        compare(activity.readingDeltas[0].cover, "")
    }

    // ---- §22 "unrendered request emits zero" / duplicate suppression at the shell seam -------

    function test_empty_or_null_pagekeys_records_nothing() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3) })
        shell._onActivityPagesPresented([])
        shell._onActivityPagesPresented(null)
        shell._onActivityPagesPresented([-1])   // a surface reporting "nothing rendered"
        compare(activity.readingDeltas.length, 0)
    }

    function test_no_activity_when_activity_store_is_absent() {
        // §25 fail-closed: an unbound ActivityStore must never break reading, and must never
        // route into a null activity object.
        var shell = makeShell({ "activity": null, "pageStore": makeStore(3) })
        shell._onActivityPagesPresented([0])   // must not throw
        verify(true, "recording against a null activity must be a silent no-op")
    }

    // ---- §9 "when hasFixedCoverage first becomes true, record media_completed" + the
    // fast-jump negative control, in ONE test so the negative half is provably not vacuous ----

    function test_coverage_completion_fires_once_and_a_last_page_jump_alone_does_not_complete() {
        var activity = createTemporaryObject(activityComp, testCase)
        var shell = makeShell({ "activity": activity, "pageStore": makeStore(3) })   // max = 3

        // NEGATIVE CONTROL: a fast jump straight to the LAST page (index 2) alone must not
        // complete the entry — only the jump target was ever recorded as read, pages 0/1 were
        // never presented. If this assertion were vacuously true (e.g. hasFixedCoverage
        // ignoring its required set, or the shell never checking coverage at all) the SECOND
        // half of this test below — recording the missing pages and expecting completion to
        // fire — would also read completions.length === 0, proving the check is live.
        shell._onActivityPagesPresented([2])
        compare(activity.completions.length, 0,
                "landing on the last page alone must not complete the entry")

        // Now genuinely cover the rest of the entry.
        shell._onActivityPagesPresented([0])
        compare(activity.completions.length, 0, "still short one page")
        shell._onActivityPagesPresented([1])
        compare(activity.completions.length, 1,
                "full non-broken physical-page coverage must complete the entry exactly once")
        compare(activity.completions[0].reason, "full_page_coverage")
        compare(activity.completions[0].kind, "manga_chapter")
        compare(activity.completions[0].itemKey, "ch1")

        // Re-presenting an already-covered page must not re-fire completion.
        shell._onActivityPagesPresented([1])
        compare(activity.completions.length, 1, "completion must fire exactly once per entry")
    }

    // ============================================================================
    // Layer 2 — per-surface emission RULES, mirrored locally (§9 Lane C / §22)
    // ============================================================================

    // ---- Single: "the successfully rendered physical page", never the error placard ----------
    QtObject {
        id: singleMirror
        property bool active: true
        property bool rendered: false   // previewImage.status===Ready || hqImage.status===Ready
        property int currentPage: 1
        property int _activityPresentedPage: -1
        property var calls: []
        // Mirrors ComicReaderSingleSurface.qml's _noteActivityPresented() body exactly.
        function notePresented() {
            if (!active || !rendered) return
            if (_activityPresentedPage === currentPage) return
            _activityPresentedPage = currentPage
            calls.push(currentPage - 1)
        }
    }

    function test_single_unrendered_request_emits_zero() {
        singleMirror.calls = []
        singleMirror._activityPresentedPage = -1
        singleMirror.rendered = false
        singleMirror.currentPage = 1
        singleMirror.notePresented()
        compare(singleMirror.calls.length, 0)
    }

    function test_single_rendered_page_emits_one_and_does_not_repeat() {
        singleMirror.calls = []
        singleMirror._activityPresentedPage = -1
        singleMirror.currentPage = 3
        singleMirror.rendered = true
        singleMirror.notePresented()
        singleMirror.notePresented()   // a repeat re-check for the SAME page: no duplicate spam
        compare(singleMirror.calls, [2])
    }

    // ---- Double: each successfully rendered member of the resolved spread, 0/1/2 as
    // applicable — error/terminal placeholder excluded from the PAYLOAD even though it still
    // counts toward the spread RESOLVING ----------------------------------------------------
    QtObject {
        id: doubleMirror
        property bool active: true
        property bool isPair: false
        property bool isSingle: false
        property int rightIndex: -1
        property int leftIndex: -1
        property bool rightResolved: false   // rightHalfResolved: pixels-up OR terminal error
        property bool leftResolved: false
        property bool rightPixelsShown: false
        property bool leftPixelsShown: false
        property int currentPage: 1
        property int _activityPresentedAnchor: -1
        property var calls: []
        readonly property bool contentOnScreen:
            (isPair || isSingle) && rightResolved && (!isPair || leftResolved)
        // Mirrors ComicReaderDoubleSurface.qml's _noteActivityPresented() body exactly.
        function notePresented() {
            if (!active || !contentOnScreen) return
            if (_activityPresentedAnchor === currentPage) return
            _activityPresentedAnchor = currentPage
            var pages = []
            if ((isPair || isSingle) && rightPixelsShown && rightIndex >= 0) pages.push(rightIndex)
            if (isPair && leftPixelsShown && leftIndex >= 0) pages.push(leftIndex)
            if (pages.length) calls.push(pages)
        }
    }

    function test_double_two_successful_spread_members_add_two() {
        doubleMirror.calls = []
        doubleMirror._activityPresentedAnchor = -1
        doubleMirror.isPair = true; doubleMirror.isSingle = false
        doubleMirror.rightIndex = 5; doubleMirror.leftIndex = 4
        doubleMirror.rightResolved = true; doubleMirror.leftResolved = true
        doubleMirror.rightPixelsShown = true; doubleMirror.leftPixelsShown = true
        doubleMirror.currentPage = 5
        doubleMirror.notePresented()
        compare(doubleMirror.calls, [[5, 4]])
    }

    function test_double_one_broken_half_adds_only_the_good_page() {
        doubleMirror.calls = []
        doubleMirror._activityPresentedAnchor = -1
        doubleMirror.isPair = true; doubleMirror.isSingle = false
        doubleMirror.rightIndex = 5; doubleMirror.leftIndex = 4
        // spread RESOLVES (both halves accounted for — the left via its terminal placard)...
        doubleMirror.rightResolved = true; doubleMirror.leftResolved = true
        // ...but only the right half genuinely rendered pixels.
        doubleMirror.rightPixelsShown = true; doubleMirror.leftPixelsShown = false
        doubleMirror.currentPage = 5
        doubleMirror.notePresented()
        compare(doubleMirror.calls, [[5]])
    }

    function test_double_fully_broken_spread_emits_zero() {
        doubleMirror.calls = []
        doubleMirror._activityPresentedAnchor = -1
        doubleMirror.isPair = true; doubleMirror.isSingle = false
        doubleMirror.rightIndex = 5; doubleMirror.leftIndex = 4
        doubleMirror.rightResolved = true; doubleMirror.leftResolved = true   // resolved via error
        doubleMirror.rightPixelsShown = false; doubleMirror.leftPixelsShown = false
        doubleMirror.currentPage = 5
        doubleMirror.notePresented()
        compare(doubleMirror.calls.length, 0)
    }

    function test_double_unresolved_spread_emits_zero() {
        doubleMirror.calls = []
        doubleMirror._activityPresentedAnchor = -1
        doubleMirror.isPair = true; doubleMirror.isSingle = false
        doubleMirror.rightIndex = 5; doubleMirror.leftIndex = 4
        doubleMirror.rightResolved = true; doubleMirror.leftResolved = false   // still decoding
        doubleMirror.rightPixelsShown = true; doubleMirror.leftPixelsShown = false
        doubleMirror.currentPage = 5
        doubleMirror.notePresented()
        compare(doubleMirror.calls.length, 0, "an unresolved spread must not emit a partial fact")
    }

    // ---- Strip: only a USER-DRIVEN move into a NEW rendered centre page; edge peeks and
    // programmatic resume/compensation never reach this check at all (§9 Lane C) --------------
    QtObject {
        id: stripMirror
        property var renderedPages: ({})     // idx0 -> bool, mirrors list.itemAtIndex(idx0).pixelsShown
        property int _activityLastCentrePage: -1
        property var calls: []
        function pageRendered(idx0) { return !!renderedPages[idx0] }
        // Mirrors ComicReaderStripSurface.qml's _noteActivityCentre() body exactly. Called ONLY
        // from the user-driven scroll path (_emitUserScroll) in production — haltScrollAt()/
        // onActiveChanged (resume, layout compensation, mount) call _emitPresented() directly
        // and NEVER this function, which is why "programmatic move" has no boolean flag to
        // toggle here at all: the asymmetry is which function gets called, not a guard inside
        // one function. userScrollCentre()/programmaticSeek() below model that exact split.
        function noteCentre(idx0) {
            if (idx0 === _activityLastCentrePage) return
            if (!pageRendered(idx0)) return
            _activityLastCentrePage = idx0
            calls.push(idx0)
        }
        // The user-driven entry point (mirrors _emitUserScroll's `root._noteActivityCentre(idx)`
        // call, reached only for the viewport CENTRE index).
        function userScrollCentre(idx0) { noteCentre(idx0) }
        // The programmatic entry point (mirrors haltScrollAt()/onActiveChanged, which call
        // _emitPresented() for the RESUME signal but never touch activity bookkeeping at all).
        function programmaticSeek(idx0) { /* deliberately does nothing to `calls` */ }
    }

    function test_strip_centre_entering_a_rendered_page_adds_one() {
        stripMirror.calls = []
        stripMirror._activityLastCentrePage = -1
        stripMirror.renderedPages = { 3: true }
        stripMirror.userScrollCentre(3)
        compare(stripMirror.calls, [3])
    }

    function test_strip_unrendered_centre_adds_zero() {
        stripMirror.calls = []
        stripMirror._activityLastCentrePage = -1
        stripMirror.renderedPages = { 3: false }
        stripMirror.userScrollCentre(3)
        compare(stripMirror.calls.length, 0)
    }

    function test_strip_edge_peek_never_reaches_the_check() {
        // A page only ever visible at the EDGE of the viewport has no call site into
        // _noteActivityCentre at all in production (only the centre index is ever passed) — so
        // the "peek" IS simply never calling userScrollCentre() for it, whatever its rendered
        // state. Modelled here by rendering page 7 but only ever asking about the true centre.
        stripMirror.calls = []
        stripMirror._activityLastCentrePage = -1
        stripMirror.renderedPages = { 3: true, 7: true }   // 7 = the merely-visible edge page
        stripMirror.userScrollCentre(3)                    // 3 = the true centre
        compare(stripMirror.calls, [3], "only the centre page may ever appear")
    }

    function test_strip_programmatic_resume_adds_zero() {
        // §9 Lane C: "Programmatic resume/layout compensation -> no activity fact."
        stripMirror.calls = []
        stripMirror._activityLastCentrePage = -1
        stripMirror.renderedPages = { 3: true }
        stripMirror.programmaticSeek(3)   // haltScrollAt()/onActiveChanged's own path
        compare(stripMirror.calls.length, 0)
        // ...and it must not have consumed the dedupe marker either — a REAL later user scroll
        // onto the same page must still be free to fire.
        stripMirror.userScrollCentre(3)
        compare(stripMirror.calls, [3])
    }

    function test_strip_stationary_repeat_does_not_spam_but_a_genuine_bounce_reaches_the_ledger() {
        // "avoid obviously duplicate spam" (surface-level politeness) vs. correctness (the
        // projector's sessionId+kind+itemKey+pageKey dedupe, §10) — this test proves BOTH
        // halves of that split: sitting still never re-fires, but a real back-and-forth does
        // (harmlessly — a real projector would dedupe the repeated "p3").
        stripMirror.calls = []
        stripMirror._activityLastCentrePage = -1
        stripMirror.renderedPages = { 3: true, 4: true }
        stripMirror.userScrollCentre(3)
        stripMirror.userScrollCentre(3)   // stationary: no scroll actually happened
        compare(stripMirror.calls, [3], "sitting still on the same centre must not re-fire")
        stripMirror.userScrollCentre(4)   // scrolled on
        stripMirror.userScrollCentre(3)   // bounced back — a genuine re-entry of page 3
        compare(stripMirror.calls, [3, 4, 3],
                "a genuine bounce back onto an already-read page IS allowed to reach the ledger "
                + "again — the projector, not this surface, is where that dedupes")
    }

    function test_strip_broken_placeholder_adds_zero() {
        stripMirror.calls = []
        stripMirror._activityLastCentrePage = -1
        stripMirror.renderedPages = { 3: false }   // hasError -> pixelsShown false
        stripMirror.userScrollCentre(3)
        compare(stripMirror.calls.length, 0)
    }
}
