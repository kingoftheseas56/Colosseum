import QtQuick
import "ProfileDeactivation.js" as ProfileDeactivation

// Shared two-phase handoff used by both Theatre players. A failed profile
// transition never closes the canonical Activity session.
QtObject {
    id: root

    required property var profileRuntime
    required property var profileTrackers
    required property var activityTracker
    required property string source
    required property string activeKey
    required property string sessionId
    required property real positionMs
    required property real durationMs

    signal activitySessionCommitted()

    property var profileRuntimeConnections: Connections {
        target: root.profileRuntime

        function onProfileDeactivationRequested() {
            if (!root.activeKey.length)
                return
            const completedLocally = root.activityTracker
                    && typeof root.activityTracker.localCompletionPersisted === "function"
                    ? root.activityTracker.localCompletionPersisted() : false
            ProfileDeactivation.snapshotTrackerPosition(
                root.profileTrackers, root.source, root.sessionId,
                root.profileTrackers ? root.profileTrackers.playbackScopeGeneration : 0,
                root.positionMs, root.durationMs, completedLocally)
        }

        function onProfileDeactivationCommitted() {
            if (!root.activeKey.length)
                return
            if (ProfileDeactivation.endActivityAfterCommit(
                    root.activityTracker, root.positionMs, root.durationMs)) {
                root.activitySessionCommitted()
            }
        }
    }
}
