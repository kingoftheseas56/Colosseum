import QtQuick
import QtTest 1.3

TestCase {
    name: "TrackerSyncCenterModelBoundary"

    function assertKeys(value, expected) {
        var actualKeys = Object.keys(value).sort()
        var expectedKeys = expected.sort()
        compare(actualKeys.join("|"), expectedKeys.join("|"))
    }

    function test_empty_catalogue_is_safe_and_truthful() {
        verify(TrackerSyncCenter !== null)
        compare(TrackerSyncCenter.connectedTrackers.length, 0)
        compare(TrackerSyncCenter.catalogue.length, 4)
        compare(TrackerSyncCenter.globalSettings.profileAvailable, true)
        compare(TrackerSyncCenter.aggregateState.status, "Empty")
        assertKeys(TrackerSyncCenter.globalSettings, [
            "trackerSyncEnabled", "checkOnLaunch", "backgroundDelivery",
            "completionMessages", "profileAvailable", "editable"])
        assertKeys(TrackerSyncCenter.aggregateState, [
            "status", "healthy", "ownerHealthy", "ownerUnavailable", "syncing",
            "connectedCount", "waitingCount", "unresolvedCount", "attentionProviderCount",
            "canSyncAll", "currentActionKind", "revision"])

        for (var i = 0; i < TrackerSyncCenter.catalogue.length; ++i) {
            var row = TrackerSyncCenter.catalogue[i]
            verify(row.providerKey.length > 0)
            verify(row.providerName.length > 0)
            verify(!("remoteAccountId" in row))
            verify(!("credential" in row))
            verify(!("payload" in row))
            assertKeys(row, [
                "providerKey", "providerName", "available", "status", "capabilities",
                "connected", "pendingWork", "waitingCount", "unresolvedCount", "connectEnabled"])
        }

        var dossier = TrackerSyncCenter.providerDossier("simkl")
        compare(dossier.found, true)
        compare(dossier.available, false)
        verify(!("remoteAccountId" in dossier))
        verify(!("token" in dossier))
        assertKeys(dossier, [
            "found", "revision", "providerKey", "providerName", "available", "status",
            "connected", "accountLabel", "capabilities", "lastSuccessfulSyncAtMs",
            "waitingCount", "pendingCount", "unresolvedCount", "knownUnsentCount",
            "unknownOutcomeCount", "importedHistoryCount", "importedProgressCount",
            "importedDataRemovalPreview", "pullAutomatically", "pullSettingEnabled",
            "sendProgressEnabled", "sendSettingEnabled", "exportReviewed",
            "exportReviewEnabled", "livePlaybackTrackingEnabled",
            "livePlaybackSettingEnabled", "connectEnabled", "pendingWork", "syncEnabled",
            "disconnectEnabled", "cleanupEnabled", "removeImportedEnabled",
            "removeImportedPending", "disconnectInFlight"])
    }

    function test_settings_use_revision_fenced_native_intents() {
        var oldRevision = TrackerSyncCenter.revision
        verify(TrackerSyncCenter.setGlobalSetting("completionMessages", true, oldRevision))
        compare(TrackerSyncCenter.globalSettings.completionMessages, true)
        assertKeys(TrackerSyncCenter.lastActionResult,
            ["accepted", "action", "code", "revision"])
        verify(!TrackerSyncCenter.setGlobalSetting("completionMessages", false, oldRevision))
        compare(TrackerSyncCenter.lastActionResult.code, "stale_intent")
        compare(TrackerSyncCenter.globalSettings.completionMessages, true)
    }
}
