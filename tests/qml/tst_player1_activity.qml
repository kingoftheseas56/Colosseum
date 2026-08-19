import QtQuick 2.15
import QtTest 1.3
import "../../qml/ActivityLaneHelpers.js" as ActivityLaneHelpers
import "../../qml/EpisodeBrowser.js" as EpisodeBrowser

// Slice D5 — Lane A (Theatre Player 1) activity hook regression, CPP-PORT-CONTRACT.md §7/§8/§9.
//
// qml/PlayerPage.qml itself is not loaded by the generic QuickTest runner because
// Colosseum.Player AND Colosseum.Activity are C++ types registered only inside
// native/main.cpp's qmlRegisterType() calls, not through a qmldir the offscreen runner can
// resolve (see tests/qml/tst_watchparty_source_provenance.qml's header for the same rule
// applied to AddonClient.js). So this suite tests the exact PURE decision logic
// PlayerPage.qml's activityBeginIfNeeded()/activitySample()/activityDiscontinuity() hooks
// are built on — qml/ActivityLaneHelpers.js — directly, plus a small local mirror of the
// hook wiring driven by a RECORDING FAKE TRACKER, so the begin/no-op/end/sample/
// discontinuity call routing is proven independent of mpv/MpvItem.
TestCase {
    name: "Player1Activity"

    // ---- §7 identity derivation (videoIdentityFor) --------------------------------------

    function test_movie_identity() {
        var idf = ActivityLaneHelpers.videoIdentityFor("tt1234567", EpisodeBrowser)
        verify(idf !== null)
        compare(idf.kind, "movie")
        compare(idf.titleKey, "theatre:tt1234567")
        compare(idf.itemKey, "tt1234567")
    }

    function test_episode_identity_derives_series_root() {
        var idf = ActivityLaneHelpers.videoIdentityFor("tt7654321:2:5", EpisodeBrowser)
        verify(idf !== null)
        compare(idf.kind, "episode")
        // titleKey groups by the SERIES root, never the individual episode id (§7: "Never
        // group by title text" / use the canonical root so S2E5 and S2E6 share one title).
        compare(idf.titleKey, "theatre:tt7654321")
        compare(idf.itemKey, "tt7654321:2:5")
    }

    function test_different_episodes_of_same_series_share_title_key_but_not_item_key() {
        var a = ActivityLaneHelpers.videoIdentityFor("tt7654321:1:1", EpisodeBrowser)
        var b = ActivityLaneHelpers.videoIdentityFor("tt7654321:1:2", EpisodeBrowser)
        compare(a.titleKey, b.titleKey)
        verify(a.itemKey !== b.itemKey)
    }

    function test_iptv_local_arriving_and_empty_ids_fail_closed_to_null() {
        // §25 fail-closed: no stable cross-session identity -> no activity fact, never an
        // invented one. Live TV is not a movie/episode; local:/arriving: are placeholder ids
        // used before a real id lands (see PlayerPage.qml's playLocalFile/playRemoteUrl).
        verify(ActivityLaneHelpers.videoIdentityFor("iptv:channel-42", EpisodeBrowser) === null)
        verify(ActivityLaneHelpers.videoIdentityFor("local:C:/movies/thing.mkv", EpisodeBrowser) === null)
        verify(ActivityLaneHelpers.videoIdentityFor("arriving:https://example/x.mkv", EpisodeBrowser) === null)
        verify(ActivityLaneHelpers.videoIdentityFor("", EpisodeBrowser) === null)
        verify(ActivityLaneHelpers.videoIdentityFor(undefined, EpisodeBrowser) === null)
    }

    // ---- §9 Lane A begin/no-op/end state-transition rule (decideTransition) -------------

    function test_null_identity_ends_session() {
        compare(ActivityLaneHelpers.decideTransition("movie|theatre:tt1|tt1", null), "end")
    }

    function test_same_item_reload_is_a_noop() {
        // A recovery/reseek/stream-replacement reload of the SAME item must NOT fragment the
        // session or the 10s activation gate (§8 "10-second activation gate": "Pause, seek,
        // buffering, and recovery do not erase activity already earned... unless the media
        // item/session itself ends").
        var idf = ActivityLaneHelpers.videoIdentityFor("tt1234567", EpisodeBrowser)
        var key = ActivityLaneHelpers.keyFor(idf)
        compare(ActivityLaneHelpers.decideTransition(key, idf), "noop")
    }

    function test_different_item_begins_a_new_session() {
        var oldKey = ActivityLaneHelpers.keyFor(ActivityLaneHelpers.videoIdentityFor("tt1111111", EpisodeBrowser))
        var newIdf = ActivityLaneHelpers.videoIdentityFor("tt2222222", EpisodeBrowser)
        compare(ActivityLaneHelpers.decideTransition(oldKey, newIdf), "begin")
    }

    function test_first_open_with_no_prior_session_begins() {
        compare(ActivityLaneHelpers.decideTransition("", ActivityLaneHelpers.videoIdentityFor("tt1234567", EpisodeBrowser)), "begin")
    }

    // ---- hook wiring against a recording fake tracker ------------------------------------
    // Mirrors PlayerPage.qml's activityBeginIfNeeded()/activitySample()/activityDiscontinuity()
    // exactly (same ActivityLaneHelpers calls, same activityActiveKey bookkeeping) but against
    // a plain QtObject fake instead of the real C++ ActivityPlaybackTracker/mpv, so the ROUTING
    // (which calls fire, in what order, with what args) is provable offscreen.
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
        property string mediaId: ""
        function activityBeginIfNeeded() {
            var idf = ActivityLaneHelpers.videoIdentityFor(lane.mediaId, EpisodeBrowser)
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
        function activityEndSession() {
            if (!lane.activityActiveKey.length) return
            fakeTracker.endSession()
            lane.activityActiveKey = ""
        }
    }

    function init() {
        fakeTracker.calls = []
        lane.activityActiveKey = ""
        lane.mediaId = ""
    }

    function test_begin_fires_once_for_a_new_item() {
        lane.mediaId = "tt1234567"
        lane.activityBeginIfNeeded()
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "begin")
        compare(fakeTracker.calls[0].identity.itemKey, "tt1234567")
    }

    function test_reload_of_same_item_does_not_begin_again() {
        lane.mediaId = "tt1234567"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.activityBeginIfNeeded()   // recovery/reload of the SAME identity
        compare(fakeTracker.calls.length, 0)
    }

    function test_item_change_ends_old_session_then_begins_new_one() {
        lane.mediaId = "tt1111111"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.mediaId = "tt2222222"
        lane.activityBeginIfNeeded()
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].op, "endSession")
        compare(fakeTracker.calls[1].op, "begin")
        compare(fakeTracker.calls[1].identity.itemKey, "tt2222222")
    }

    function test_switching_to_iptv_ends_the_open_session_without_a_new_begin() {
        lane.mediaId = "tt1234567"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.mediaId = "iptv:channel-42"
        lane.activityBeginIfNeeded()
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "endSession")
    }

    function test_discontinuity_and_sample_are_no_ops_before_any_session_begins() {
        // §25 fail-closed: an unbound/not-yet-open lane must never route a stray sample/
        // discontinuity call into a fact for no identity.
        lane.activityDiscontinuity(5000)
        lane.activitySample(true)
        compare(fakeTracker.calls.length, 0)
    }

    function test_sample_forwards_consuming_flag() {
        lane.mediaId = "tt1234567"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.activitySample(true)
        lane.activitySample(false)
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].consuming, true)
        compare(fakeTracker.calls[1].consuming, false)
    }

    function test_discontinuity_forwards_the_authoritative_seek_target() {
        // The central seekTo() path passes the seek TARGET explicitly (not a stale
        // mpv.position that has not caught up to the async seek yet).
        lane.mediaId = "tt1234567"
        lane.activityBeginIfNeeded()
        fakeTracker.calls = []
        lane.activityDiscontinuity(42000)
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "discontinuity")
        compare(fakeTracker.calls[0].positionMs, 42000)
    }
}
