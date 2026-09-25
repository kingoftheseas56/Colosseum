.pragma library

// Profile changes are a two-phase boundary. Snapshotting is non-mutating for
// Activity; the native owner emits commit only after external close intents
// are durably safe. On refusal, the live Activity session keeps sampling.
function snapshotTrackerPosition(trackers, source, sessionId, scopeGeneration,
                                 positionMs, durationMs, completedLocally) {
    if (!trackers || !sessionId || !trackers.snapshotPlaybackPosition)
        return false
    return trackers.snapshotPlaybackPosition(source, sessionId, scopeGeneration,
                                             positionMs, durationMs, completedLocally)
}

function endActivityAfterCommit(activityTracker, positionMs, durationMs) {
    if (!activityTracker || !activityTracker.endSessionForProfileDeactivation)
        return false
    activityTracker.endSessionForProfileDeactivation(positionMs, durationMs)
    return true
}
