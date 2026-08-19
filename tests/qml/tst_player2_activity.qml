import QtQuick 2.15
import QtTest 1.3
import "../../qml/ActivityLaneHelpers.js" as ActivityLaneHelpers
import "../../qml/Player2ActivityHelpers.js" as Player2ActivityHelpers

// Slice D6 — Lane B (Theatre Player 2) activity hook regression, CPP-PORT-CONTRACT.md §7/§8/§9.
//
// qml/player2/Player2Shell.qml itself is not loaded by the generic QuickTest runner because it
// imports Colosseum.Activity, a C++ type registered only inside native/main.cpp's
// qmlRegisterType() call, not through a qmldir the offscreen runner can resolve (the same
// constraint tests/qml/tst_player1_activity.qml documents for qml/PlayerPage.qml and
// tests/qml/tst_audiobook_activity.qml documents for qml/AudiobookSession.qml). So this suite
// tests the exact PURE decision logic Player2Shell.qml's activityBeginIfNeeded()/
// activitySample()/activityDiscontinuity()/activityNaturalEof() hooks are built on —
// qml/Player2ActivityHelpers.js's videoIdentityFor() plus the SHARED
// qml/ActivityLaneHelpers.js decideTransition()/keyFor() every lane calls — plus a small local
// mirror of the hook wiring driven by a RECORDING FAKE TRACKER and a fake typed session-state
// driver, so the begin/no-op/end/sample/discontinuity/naturalEof call routing — and in
// particular the §9 Lane B rule "consuming is driven exclusively from state == Playing &&
// !networkStalled" — is proven independent of the native Player2Session/mpv.
TestCase {
    name: "Player2Activity"

    // ---- §7 identity derivation (videoIdentityFor) --------------------------------------

    function test_movie_identity_uses_root_as_item_key() {
        // No currentEpisodeId -> movie, itemKey falls back to the root id itself.
        var idf = Player2ActivityHelpers.videoIdentityFor("tt1234567", "")
        verify(idf !== null)
        compare(idf.kind, "movie")
        compare(idf.titleKey, "theatre:tt1234567")
        compare(idf.itemKey, "tt1234567")
    }

    function test_episode_identity_uses_root_for_titlekey_episode_for_itemkey() {
        var idf = Player2ActivityHelpers.videoIdentityFor("tt7654321", "tt7654321:2:5")
        verify(idf !== null)
        compare(idf.kind, "episode")
        compare(idf.titleKey, "theatre:tt7654321")
        compare(idf.itemKey, "tt7654321:2:5")
    }

    function test_autoplay_to_next_episode_shares_title_key_but_not_item_key() {
        // §9 Lane B: "autoplay changes item identity without losing title grouping."
        var a = Player2ActivityHelpers.videoIdentityFor("tt7654321", "tt7654321:1:1")
        var b = Player2ActivityHelpers.videoIdentityFor("tt7654321", "tt7654321:1:2")
        compare(a.titleKey, b.titleKey)
        verify(a.itemKey !== b.itemKey)
    }

    function test_empty_root_media_id_fails_closed_to_null() {
        // §25 fail-closed: no stable identity yet (metadata not landed) -> no activity fact.
        verify(Player2ActivityHelpers.videoIdentityFor("", "") === null)
        verify(Player2ActivityHelpers.videoIdentityFor(undefined, undefined) === null)
        verify(Player2ActivityHelpers.videoIdentityFor("", "tt1:1:1") === null)
    }

    // ---- §9 Lane B begin/no-op/end state-transition rule (shared with Lane A/E) ---------

    function test_null_identity_ends_session() {
        compare(ActivityLaneHelpers.decideTransition("movie|theatre:tt1|tt1", null), "end")
    }

    function test_same_identity_reload_is_a_noop() {
        var idf = Player2ActivityHelpers.videoIdentityFor("tt1234567", "")
        var key = ActivityLaneHelpers.keyFor(idf)
        compare(ActivityLaneHelpers.decideTransition(key, idf), "noop")
    }

    function test_different_root_begins_a_new_session() {
        var oldKey = ActivityLaneHelpers.keyFor(Player2ActivityHelpers.videoIdentityFor("tt1111111", ""))
        var newIdf = Player2ActivityHelpers.videoIdentityFor("tt2222222", "")
        compare(ActivityLaneHelpers.decideTransition(oldKey, newIdf), "begin")
    }

    // ---- hook wiring against a recording fake tracker + a fake typed session state driver -

    // Mirrors Player2Shell.qml's activityBeginIfNeeded()/activitySample()/activityDiscontinuity()/
    // activityNaturalEof()/activityEndSession() exactly (same ActivityLaneHelpers/
    // Player2ActivityHelpers calls, same activityActiveKey bookkeeping, same
    // "consuming = state===Playing && !networkStalled" rule) against a plain QtObject fake
    // tracker instead of the real C++ ActivityPlaybackTracker/Player2Session, so the ROUTING
    // (which calls fire, in what order, with what args) is provable offscreen. Player2State
    // values mirror native/player2/core/Player2Types.h's Q_ENUM_NS ordering exactly:
    // Idle=0, Opening=1, Buffering=2, Playing=3, Paused=4, Seeking=5, Ended=6, Recovering=7,
    // Error=8 (also cross-checked against Player2Shell.qml's own state===4 Paused / excluded
    // 1,2,5,8 pauseCardEligible guard).
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
        id: fakeSession
        property int state: 0          // Idle
        property real position: 0
        property real duration: 0
        property real speed: 1
        property bool networkStalled: false
    }
    QtObject {
        id: shell
        property string activityActiveKey: ""
        property string rootMediaId: ""
        property string currentEpisodeId: ""
        property string mediaTitle: ""
        property string mediaSubtitle: ""
        property string mediaLogo: ""
        property var session: fakeSession

        function activityBeginIfNeeded() {
            var idf = Player2ActivityHelpers.videoIdentityFor(shell.rootMediaId, shell.currentEpisodeId)
            var action = ActivityLaneHelpers.decideTransition(shell.activityActiveKey, idf)
            if (action === "noop")
                return
            shell.activityEndSession()
            if (action === "end")
                return
            shell.activityActiveKey = ActivityLaneHelpers.keyFor(idf)
            fakeTracker.begin({ "kind": idf.kind, "titleKey": idf.titleKey, "itemKey": idf.itemKey }, "sess-1")
        }
        function activitySample() {
            if (!shell.activityActiveKey.length) return
            var s = shell.session
            if (!s) return
            var consuming = (s.state === 3) && !s.networkStalled
            fakeTracker.sample(Math.round(s.position * 1000), Math.round(s.duration * 1000),
                                Math.round(s.speed * 1000), consuming)
        }
        function activityDiscontinuity() {
            if (!shell.activityActiveKey.length) return
            var s = shell.session
            if (!s) return
            fakeTracker.discontinuity(Math.round(s.position * 1000), Math.round(s.duration * 1000),
                                       Math.round(s.speed * 1000))
        }
        function activityNaturalEof() {
            if (!shell.activityActiveKey.length) return
            fakeTracker.naturalEof()
            fakeTracker.endSession()
            shell.activityActiveKey = ""
        }
        function activityEndSession() {
            if (!shell.activityActiveKey.length) return
            fakeTracker.endSession()
            shell.activityActiveKey = ""
        }
        // Mirrors the shell's Connections{target: session}.onStateChanged exactly.
        function onSessionStateChanged() {
            var st = shell.session ? shell.session.state : -1
            if (st === 3)        // Playing
                shell.activityBeginIfNeeded()
            else if (st === 6)   // Ended
                shell.activityNaturalEof()
            else
                shell.activityDiscontinuity()
        }
    }

    // Drives fakeSession.state through the fake shell's onSessionStateChanged mirror (a plain
    // QtObject property write does not fire a QML Connections handler by itself, so the test
    // calls the mirror function explicitly right after each state write — exactly what the
    // real Connections{target:session; function onStateChanged()} does when the native signal
    // fires).
    function setState(st) {
        fakeSession.state = st
        shell.onSessionStateChanged()
    }

    function init() {
        fakeTracker.calls = []
        shell.activityActiveKey = ""
        shell.rootMediaId = ""
        shell.currentEpisodeId = ""
        fakeSession.state = 0
        fakeSession.position = 0
        fakeSession.duration = 0
        fakeSession.speed = 1
        fakeSession.networkStalled = false
    }

    // ---- §22 Player 2 proofs -------------------------------------------------------------

    function test_only_playing_state_contributes() {
        shell.rootMediaId = "tt1234567"
        setState(3)   // Playing -> begins
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "begin")
        fakeTracker.calls = []

        fakeSession.position = 1
        fakeSession.duration = 10
        shell.activitySample()
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "sample")
        compare(fakeTracker.calls[0].consuming, true)
    }

    function test_seeking_buffering_recovering_paused_ended_error_stalled_do_not_consume() {
        shell.rootMediaId = "tt1234567"
        setState(3)   // Playing -> begins the session once
        fakeTracker.calls = []

        var nonConsumingStates = [1, 2, 4, 5, 7, 8]   // Opening, Buffering, Paused, Seeking, Recovering, Error
        for (var i = 0; i < nonConsumingStates.length; i++) {
            fakeSession.state = nonConsumingStates[i]
            shell.activitySample()
            compare(fakeTracker.calls[fakeTracker.calls.length - 1].consuming, false)
        }

        // Playing but network-stalled must also not consume (§9 Lane B qualifying state).
        fakeSession.state = 3
        fakeSession.networkStalled = true
        shell.activitySample()
        compare(fakeTracker.calls[fakeTracker.calls.length - 1].consuming, false)

        // Ended is reached via naturalEof(), never a plain sample() call, but confirm the
        // qualifying-state formula itself would reject it too.
        fakeSession.networkStalled = false
        fakeSession.state = 6
        shell.activitySample()
        compare(fakeTracker.calls[fakeTracker.calls.length - 1].consuming, false)
    }

    function test_state_transitions_reset_the_baseline() {
        shell.rootMediaId = "tt1234567"
        setState(3)   // begins
        fakeTracker.calls = []

        setState(5)   // Seeking: any non-Playing/non-Ended entry -> discontinuity
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "discontinuity")

        fakeTracker.calls = []
        setState(3)   // back to Playing: same identity still open -> noop, no new begin
        compare(fakeTracker.calls.length, 0)
    }

    function test_generation_change_resets_the_baseline() {
        shell.rootMediaId = "tt1234567"
        setState(3)
        fakeTracker.calls = []
        shell.activityDiscontinuity()   // mirrors Connections.onGenerationChanged
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "discontinuity")
    }

    function test_seek_completed_resets_the_baseline() {
        shell.rootMediaId = "tt1234567"
        setState(3)
        fakeTracker.calls = []
        shell.activityDiscontinuity()   // mirrors Connections.onSeekCompleted
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "discontinuity")
    }

    function test_speed_change_resets_the_baseline() {
        shell.rootMediaId = "tt1234567"
        setState(3)
        fakeTracker.calls = []
        fakeSession.speed = 2
        shell.activityDiscontinuity()   // mirrors Connections.onSpeedChanged
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "discontinuity")
    }

    function test_autoplay_identity_change_keeps_title_key() {
        shell.rootMediaId = "tt7654321"
        shell.currentEpisodeId = "tt7654321:1:1"
        setState(3)   // begins episode 1
        compare(fakeTracker.calls.length, 1)
        var firstTitleKey = fakeTracker.calls[0].identity.titleKey
        fakeTracker.calls = []

        // Autoplay: host advances to the next episode; the session cycles through Opening then
        // back to Playing (generationChanged/onStateChanged already reset the baseline along
        // the way — irrelevant to this assertion).
        shell.currentEpisodeId = "tt7654321:1:2"
        setState(1)   // Opening: discontinuity only, no begin yet (identity still stale here)
        fakeTracker.calls = []
        setState(3)   // Playing again: activityBeginIfNeeded sees a new itemKey -> end + begin
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].op, "endSession")
        compare(fakeTracker.calls[1].op, "begin")
        compare(fakeTracker.calls[1].identity.titleKey, firstTitleKey)
        verify(fakeTracker.calls[1].identity.itemKey !== "tt7654321:1:1")
        compare(fakeTracker.calls[1].identity.itemKey, "tt7654321:1:2")
    }

    function test_natural_ended_state_completes_and_ends_the_session() {
        shell.rootMediaId = "tt1234567"
        setState(3)
        fakeTracker.calls = []
        setState(6)   // Ended
        compare(fakeTracker.calls.length, 2)
        compare(fakeTracker.calls[0].op, "naturalEof")
        compare(fakeTracker.calls[1].op, "endSession")
        compare(shell.activityActiveKey, "")
    }

    function test_close_ends_the_open_session() {
        shell.rootMediaId = "tt1234567"
        setState(3)
        fakeTracker.calls = []
        shell.activityEndSession()   // mirrors requestClose()/CloseConfirm.onConfirmed
        compare(fakeTracker.calls.length, 1)
        compare(fakeTracker.calls[0].op, "endSession")
        compare(shell.activityActiveKey, "")
    }

    function test_sample_and_discontinuity_are_no_ops_before_any_session_begins() {
        // §25 fail-closed: an unbound/not-yet-open lane must never route a stray sample/
        // discontinuity call into a fact for no identity.
        shell.activitySample()
        shell.activityDiscontinuity()
        compare(fakeTracker.calls.length, 0)
    }
}
