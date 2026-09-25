import QtQuick 2.15
import QtTest 1.3
import "../../qml" as ColosseumQml

TestCase {
    name: "ProfileActivityDeactivation"

    QtObject {
        id: profileRuntime
        property bool closeCanCommit: false
        signal profileDeactivationRequested()
        signal profileDeactivationCommitted()

        function attemptDeactivation() {
            profileDeactivationRequested()
            if (closeCanCommit)
                profileDeactivationCommitted()
        }
    }

    QtObject {
        id: profileTrackers
        property int playbackScopeGeneration: 17
        property int snapshotCount: 0
        property string lastSource: ""
        property string lastSessionId: ""
        property real lastPositionMs: -1
        property real lastDurationMs: -1
        property bool lastCompletedLocally: false

        function snapshotPlaybackPosition(source, sessionId, scope, position, duration, completed) {
            snapshotCount++
            lastSource = source
            lastSessionId = sessionId
            lastPositionMs = position
            lastDurationMs = duration
            lastCompletedLocally = completed
            return scope === playbackScopeGeneration
        }
    }

    QtObject {
        id: activityTracker
        property bool active: true
        property bool completed: true
        property int endCount: 0
        property real endedAtPositionMs: -1
        property real endedAtDurationMs: -1
        property int sampledWhileActive: 0

        function localCompletionPersisted() { return completed }
        function endSessionForProfileDeactivation(position, duration) {
            active = false
            endCount++
            endedAtPositionMs = position
            endedAtDurationMs = duration
        }
        function sample() {
            if (active)
                sampledWhileActive++
        }
    }

    QtObject {
        id: player
        property string activeKey: "movie|theatre:fixture|fixture"
        property string sessionId: "activity-session-fixture"
        property real positionMs: 48600
        property real durationMs: 100000
        property bool activitySessionCommitted: false
    }

    ColosseumQml.ProfileActivityDeactivation {
        profileRuntime: profileRuntime
        profileTrackers: profileTrackers
        activityTracker: activityTracker
        source: "player1"
        activeKey: player.activeKey
        sessionId: player.sessionId
        positionMs: player.positionMs
        durationMs: player.durationMs
        onActivitySessionCommitted: {
            player.activitySessionCommitted = true
            player.activeKey = ""
            player.sessionId = ""
        }
    }

    function init() {
        profileRuntime.closeCanCommit = false
        profileTrackers.snapshotCount = 0
        profileTrackers.lastSource = ""
        profileTrackers.lastSessionId = ""
        profileTrackers.lastPositionMs = -1
        profileTrackers.lastDurationMs = -1
        profileTrackers.lastCompletedLocally = false
        activityTracker.active = true
        activityTracker.completed = false
        activityTracker.endCount = 0
        activityTracker.endedAtPositionMs = -1
        activityTracker.endedAtDurationMs = -1
        activityTracker.sampledWhileActive = 0
        player.activeKey = "movie|theatre:fixture|fixture"
        player.sessionId = "activity-session-fixture"
        player.positionMs = 48600
        player.durationMs = 100000
        player.activitySessionCommitted = false
    }

    function test_refused_profile_change_keeps_native_activity_sampling() {
        activityTracker.completed = true
        profileRuntime.attemptDeactivation()

        compare(profileTrackers.snapshotCount, 1)
        compare(profileTrackers.lastSource, "player1")
        compare(profileTrackers.lastSessionId, "activity-session-fixture")
        compare(profileTrackers.lastPositionMs, 48600)
        compare(profileTrackers.lastDurationMs, 100000)
        compare(profileTrackers.lastCompletedLocally, true)
        compare(activityTracker.endCount, 0)
        verify(activityTracker.active)
        verify(player.activeKey.length > 0)
        verify(!player.activitySessionCommitted)

        activityTracker.sample()
        compare(activityTracker.sampledWhileActive, 1)
    }

    function test_committed_profile_change_ends_activity_only_after_tracker_close() {
        profileRuntime.closeCanCommit = true
        profileRuntime.attemptDeactivation()

        compare(profileTrackers.snapshotCount, 1)
        compare(activityTracker.endCount, 1)
        verify(!activityTracker.active)
        compare(activityTracker.endedAtPositionMs, 48600)
        compare(activityTracker.endedAtDurationMs, 100000)
        verify(!player.activeKey.length)
        verify(!player.sessionId.length)
        verify(player.activitySessionCommitted)
    }
}
