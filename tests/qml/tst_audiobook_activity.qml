import QtQuick 2.15
import QtTest 1.3
import "../../qml/ActivityLaneHelpers.js" as ActivityLaneHelpers

// Slice D5 — Lane E (Biblio audiobook) activity hook regression, CPP-PORT-CONTRACT.md §7/§8/§9.
//
// qml/AudiobookSession.qml itself is not loaded by the generic QuickTest runner because
// Colosseum.Player AND Colosseum.Activity are C++ types registered only inside
// native/main.cpp's qmlRegisterType() calls, not through a qmldir the offscreen runner can
// resolve (see tests/qml/tst_watchparty_source_provenance.qml's header for the same rule
// applied to AddonClient.js). So this suite tests the exact PURE decision logic
// AudiobookSession.qml's activityBeginIfNeeded()/activitySample()/activityDiscontinuity()
// hooks are built on — qml/ActivityLaneHelpers.js — directly, plus a small local mirror of
// the hook wiring (including the multi-file EOF split) driven by a RECORDING FAKE TRACKER.
TestCase {
    name: "AudiobookActivity"

    // ---- §7 identity derivation (audiobookIdentityFor) -----------------------------------

    function test_audiobook_identity_uses_pairkey_as_itemkey() {
        var idf = ActivityLaneHelpers.audiobookIdentityFor("dune|frank herbert")
        verify(idf !== null)
        compare(idf.kind, "audiobook")
        compare(idf.itemKey, "dune|frank herbert")
        // Namespaced (never the raw pairKey alone) — pairKey is a normalized title|author
        // text key, not a canonical cross-service book ID (see AudiobookSession.qml), so
        // callers additionally mark the event syncable:false; this module only shapes identity.
        compare(idf.titleKey, "biblio:dune|frank herbert")
    }

    function test_empty_pairkey_fails_closed_to_null() {
        verify(ActivityLaneHelpers.audiobookIdentityFor("") === null)
        verify(ActivityLaneHelpers.audiobookIdentityFor(undefined) === null)
    }

    // ---- §9 Lane E begin/no-op/end state-transition rule (shared with Lane A) -----------

    function test_same_pairkey_reopen_is_a_noop() {
        // openFor()'s own same-pairKey/ready guard means this only matters for the second
        // line of defense, but the rule itself must hold: reopening the live pairKey must
        // never fragment the session (mirrors Lane A's "same item reload" case).
        var idf = ActivityLaneHelpers.audiobookIdentityFor("dune|frank herbert")
        var key = ActivityLaneHelpers.keyFor(idf)
        compare(ActivityLaneHelpers.decideTransition(key, idf), "noop")
    }

    function test_different_pairkey_begins_a_new_session() {
        var oldKey = ActivityLaneHelpers.keyFor(ActivityLaneHelpers.audiobookIdentityFor("dune|frank herbert"))
        var newIdf = ActivityLaneHelpers.audiobookIdentityFor("the hobbit|j r r tolkien")
        compare(ActivityLaneHelpers.decideTransition(oldKey, newIdf), "begin")
    }

    function test_closing_the_book_ends_the_session() {
        var idf = ActivityLaneHelpers.audiobookIdentityFor("dune|frank herbert")
        var key = ActivityLaneHelpers.keyFor(idf)
        compare(ActivityLaneHelpers.decideTransition(key, null), "end")
    }

    // ---- hook wiring against a recording fake tracker ------------------------------------
    // Mirrors AudiobookSession.qml's activityBeginIfNeeded()/activitySample()/
    // activityDiscontinuity()/activityNaturalEof() exactly (same ActivityLaneHelpers calls,
    // same activityActiveKey bookkeeping) but against a plain QtObject fake instead of the
    // real C++ ActivityPlaybackTracker/mpv session, so the ROUTING is provable offscreen —
    // in particular the §9 Lane E rule "intermediate multi-file EOF does not complete" vs.
    // "final-file EOF completes once", which AudiobookSession.qml's onEndFile implements by
    // routing an intermediate EOF through playIndex() (file-switch discontinuity) and only a
    // final EOF through activityNaturalEof().
    QtObject {
        id: fakeTracker
        property var calls: []
        function begin(identity, sessionId) { calls.push({ "op": "begin", "identity": identity, "sessionId": sessionId }) }
        function sample(positionMs, durationMs, rateMilli, consuming) { calls.push({ "op": "sample", "consuming": consuming }) }
        function discontinuity(positionMs, durationMs, rateMilli) { calls.push({ "op": "discontinuity", "positionMs": positionMs }) }
        function naturalEof() { calls.push({ "op": "naturalEof" }) }
        function endSession() { calls.push({ "op": "endSession" }) }
    }
    QtObject {
        id: lane
        property string activityActiveKey: ""
        property string activePairKey: ""
        function activityBeginIfNeeded() {
            var idf = ActivityLaneHelpers.audiobookIdentityFor(lane.activePairKey)
            var action = ActivityLaneHelpers.decideTransition(lane.activityActiveKey, idf)
            if (action === "noop")
                return
            lane.activityEndSession()
            if (action === "end")
                return
            lane.activityActiveKey = ActivityLaneHelpers.keyFor(idf)
            fakeTracker.begin({ "kind": idf.kind, "titleKey": idf.titleKey, "itemKey": idf.itemKey }, "sess-1")
        }
        function activitySample(consuming) {
            if (!lane.activityActiveKey.length) return
            fakeTracker.sample(1000, 2000, 1000, consuming)
        }
        function activityDiscontinuity(atMs) {
            if (!lane.activityActiveKey.length) return
            fakeTracker.discontinuity(atMs, 2000, 1000)
        }
        function activityNaturalEof() {
            if (!lane.activityActiveKey.length) return
            fakeTracker.naturalEof()
            fakeTracker.endSession()
            lane.activityActiveKey = ""
        }
        function activityEndSession() {
            if (!lane.activityActiveKey.length) return
            fakeTracker.endSession()
            lane.activityActiveKey = ""
        }
        // Mirrors onEndFile's branch: intermediate multi-file EOF is a file-switch
        // discontinuity on the SAME session; only the final file's EOF completes.
        function onEndFileEof(hasMoreFiles) {
            if (hasMoreFiles)
                lane.activityDiscontinuity(0)
            else
                lane.activityNaturalEof()
        }
    }

    function init() {
        fakeTracker.calls = []
        lane.activityActiveKey = ""
        lane.activePairKey = ""
    }

    function test_begin_fires_once_for_a_new_book() {
        lane.activePairKey = "dune|frank herbert"
        lane.activityBeginIfNeeded()
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "begin")
        compare(fakeTracker.calls[0].identity.itemKey, "dune|frank herbert")
    }

    function test_reopening_the_live_pairkey_does_not_begin_again() {
        lane.activePairKey = "dune|frank herbert"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.activityBeginIfNeeded()
        compare(fakeTracker.calls.length, 0)
    }

    function test_different_book_ends_old_session_then_begins_new_one() {
        lane.activePairKey = "dune|frank herbert"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.activePairKey = "the hobbit|j r r tolkien"
        lane.activityBeginIfNeeded()
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].op, "endSession")
        compare(fakeTracker.calls[1].op, "begin")
    }

    function test_intermediate_multi_file_eof_is_discontinuity_not_completion() {
        lane.activePairKey = "dune|frank herbert"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.onEndFileEof(true)   // more files remain in this multi-file audiobook
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "discontinuity")
        // the session must still be open — a later real completion is still possible
        verify(lane.activityActiveKey.length > 0)
    }

    function test_final_file_eof_completes_once_and_ends_the_session() {
        lane.activePairKey = "dune|frank herbert"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.onEndFileEof(false)   // last file
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].op, "naturalEof")
        compare(fakeTracker.calls[1].op, "endSession")
        compare(lane.activityActiveKey, "")
    }

    function test_sample_forwards_consuming_flag_ready_and_not_paused() {
        lane.activePairKey = "dune|frank herbert"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.activitySample(true)    // ready && !paused
        lane.activitySample(false)   // paused — the 10s heartbeat still ticks (untouched,
                                      // AUDIT.md Lane 5) but the tracker discards it
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].consuming, true)
        compare(fakeTracker.calls[1].consuming, false)
    }

    function test_discontinuity_and_sample_are_no_ops_before_any_session_begins() {
        lane.activityDiscontinuity(5000)
        lane.activitySample(true)
        compare(fakeTracker.calls.length, 0)
    }
}
