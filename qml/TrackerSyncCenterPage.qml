// Tracker Connections, Arc 35 Slice 9.
// The approved Sync Center mock supplies the visual hierarchy; all connection,
// capability, and health copy comes from the native TrackerSyncCenter model.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    objectName: "trackerSyncCenterPage"

    property Item backdrop: null
    property var stremioState: null
    property var trackerModel: (typeof TrackerSyncCenter !== "undefined")
                               ? TrackerSyncCenter : null
    property bool reducedMotion: false
    property string selectedProviderKey: ""
    property var selectedDossier: ({})
    property Item dossierInvoker: null
    property bool recoveryRouteActive: false
    property string recoveryRouteProviderKey: ""
    property int recoveryRouteFocusIndex: -1
    property var recoveryRouteRevision: -1
    property string recoveryRouteState: ""
    property string recoveryRouteReason: ""
    property int recoveryRouteGeneration: 0
    property var recoveryRouteModel: null
    property string actionNotice: ""
    property string selectedImportBatchId: ""
    property var selectedImportSnapshot: ({ accepted: false, items: [] })
    property string importReviewInvokerObjectName: ""
    property string importReviewNotice: ""
    property var selectedExportReview: ({ accepted: false, items: [] })
    property var selectedExportItemIds: []
    property var exportReviewModel: null
    property bool exportConfirmationPending: false
    property string exportReviewNotice: ""
    property var selectedBulkImportItemIds: []
    property bool bulkImportReviewOpen: false
    property string bulkImportReviewNotice: ""
    property bool titleMatchOpen: false
    property string titleMatchBatchId: ""
    property string titleMatchReviewItemId: ""
    property string titleMatchRemoteTitle: ""
    property string titleMatchSearchText: ""
    property var titleMatchRows: []
    property string titleMatchNotice: ""
    property string titleMatchInvokerObjectName: ""
    property var titleMatchModel: null
    property var titleMatchRevision: -1
    property bool titleMatchChoicePending: false
    property bool titleMatchRequiresReopen: false
    property var bulkImportReviewModel: null
    property var bulkImportReviewRevision: -1
    property string bulkImportReviewBatchId: ""
    property var bulkImportReviewItemIds: []
    property bool bulkImportChoicePending: false
    property bool disconnectReviewOpen: false
    property string disconnectReviewNotice: ""
    property var disconnectReviewModel: null
    property int disconnectReviewRevision: -1
    property string disconnectReviewProviderKey: ""
    property string disconnectReviewAccountLabel: ""
    property bool disconnectActionPending: false
    property bool importedDataReviewOpen: false
    property string importedDataReviewNotice: ""
    property var importedDataReviewModel: null
    property int importedDataReviewRevision: -1
    property string importedDataReviewProviderKey: ""
    property string importedDataReviewAccountLabel: ""
    property bool importedRemovalAwaitingResult: false

    readonly property var connectedTrackers: {
        if (!trackerModel) return []
        var revision = Number(trackerModel.revision)
        return revision >= 0 ? trackerModel.connectedTrackers : []
    }
    readonly property var catalogue: {
        if (!trackerModel) return []
        var revision = Number(trackerModel.revision)
        return revision >= 0 ? trackerModel.catalogue : []
    }
    readonly property var globalSettings: {
        if (!trackerModel) return ({})
        var revision = Number(trackerModel.revision)
        return revision >= 0 ? trackerModel.globalSettings : ({})
    }
    readonly property var aggregateState: {
        if (!trackerModel) return ({ status: "Owner unavailable", connectedCount: 0,
                                     waitingCount: 0, unresolvedCount: 0,
                                     attentionProviderCount: 0, canSyncAll: false })
        var revision = Number(trackerModel.revision)
        return revision >= 0 ? trackerModel.aggregateState
                             : ({ status: "Owner unavailable", connectedCount: 0,
                                  waitingCount: 0, unresolvedCount: 0,
                                  attentionProviderCount: 0, canSyncAll: false })
    }
    readonly property int connectedCount: Number(aggregateState.connectedCount || 0)
    readonly property int waitingCount: Number(aggregateState.waitingCount || 0)
    readonly property int unresolvedCount: Number(aggregateState.unresolvedCount || 0)
    readonly property int attentionProviderCount: Number(aggregateState.attentionProviderCount || 0)
    readonly property int attentionBadgeCount: unresolvedCount > 0
                                               ? unresolvedCount : attentionProviderCount
    readonly property bool syncing: aggregateState.syncing === true
    readonly property bool syncAllEnabled: aggregateState.canSyncAll === true
    readonly property bool stremioConnected: stremioState && stremioState.linkedAccount === true
    readonly property bool dossierOpen: selectedProviderKey.length > 0
    readonly property bool importReviewOpen: selectedImportBatchId.length > 0
    readonly property bool exportReviewOpen: selectedExportReview.accepted === true
    readonly property int connectedColumns: width >= 1100 ? 3 : (width >= 760 ? 2 : 1)
    readonly property int catalogueColumns: width >= 1180 ? 4 : (width >= 760 ? 2 : 1)
    readonly property var dossierCapabilities:
        root.capabilitiesForDisplay(selectedDossier.capabilities)
    readonly property int pageInset: Math.min(theme.margin, Math.max(22, width * 0.05))
    readonly property int taskbarBottomClearance: 80
    readonly property bool motionEnabled: !reducedMotion

    signal backRequested()
    signal mainSyncRequested()

    onTrackerModelChanged: {
        var closedReviewedAction = false
        if (disconnectReviewOpen) {
            closeDisconnectReview()
            closedReviewedAction = true
        }
        if (importedDataReviewOpen) {
            closeImportedDataReview()
            closedReviewedAction = true
        }
        if (titleMatchOpen) {
            closeTitleMatchFinder(false)
            closedReviewedAction = true
        }
        if (bulkImportReviewOpen) {
            closeBulkImportReview()
            selectedBulkImportItemIds = []
            closedReviewedAction = true
        }
        if (importReviewOpen) {
            closeImportReview()
            closedReviewedAction = true
        }
        if (exportReviewOpen) {
            closeExportReview()
            closedReviewedAction = true
        }
        if (trackerModel && dossierOpen)
            refreshDossier()
        else if (!trackerModel)
            selectedDossier = ({})
        if (closedReviewedAction)
            actionNotice = "The active Colosseum profile changed while this review was open. Open the action again to review the current profile."
    }

    Theme { id: theme }
    KeyboardSpatialNavigator { id: pageSpatialNav; root: pageScroll }

    component TrackerSettingToggle: Switch {
        id: settingToggle
        property string settingLabel: ""
        Accessible.name: settingLabel
        contentItem: Text {
            text: settingToggle.settingLabel
            color: settingToggle.enabled ? theme.ink : theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            rightPadding: settingToggle.indicator.width + settingToggle.spacing
        }
        indicator: Rectangle {
            implicitWidth: 42
            implicitHeight: 23
            x: settingToggle.width - width
            y: (settingToggle.height - height) / 2
            radius: height / 2
            color: settingToggle.checked ? theme.gold : Qt.rgba(1, 1, 1, 0.14)
            border.width: settingToggle.activeFocus ? 2 : 1
            border.color: settingToggle.activeFocus ? theme.gold : theme.edge
            Rectangle {
                width: 17
                height: 17
                radius: 9
                y: 3
                x: settingToggle.checked ? parent.width - width - 3 : 3
                color: "#141416"
                Behavior on x {
                    NumberAnimation {
                        duration: root.motionEnabled ? 140 : 0
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    function providerIcon(providerKey) {
        if (providerKey === "mal") return "../assets/icons/trackers/myanimelist.svg"
        if (providerKey === "trakt") return "../assets/icons/trackers/trakt.svg"
        if (providerKey === "simkl") return "../assets/icons/trackers/simkl.svg"
        if (providerKey === "anilist") return "../assets/icons/trackers/anilist.svg"
        return ""
    }

    function providerSubject(providerKey) {
        if (providerKey === "simkl") return "Movies, series, and anime"
        if (providerKey === "mal") return "Anime and manga"
        if (providerKey === "trakt") return "Movies and series"
        if (providerKey === "anilist") return "Anime and manga"
        return ""
    }

    function stremioStatusLabel() {
        if (!stremioState) return "Status unavailable"
        var status = String(stremioState.status || "")
        if (status === "reconnectRequired" || status === "syncFailed" || status === "paused")
            return "Needs attention"
        if (status === "connecting") return "Connecting"
        if (status === "syncing") return "Updating"
        return stremioConnected ? "Connected" : "Not connected"
    }

    function capabilityLabel(capability) {
        if (capability === "read_history") return "History import"
        if (capability === "read_progress") return "Progress import"
        if (capability === "write_progress") return "Progress export"
        if (capability === "write_completion") return "Completion export"
        if (capability === "scrobble") return "Playback tracking"
        return ""
    }

    function capabilitiesForDisplay(capabilities) {
        var result = []
        var rows = capabilities || []
        for (var i = 0; i < rows.length; ++i) {
            var label = capabilityLabel(String(rows[i]))
            if (label.length) result.push(label)
        }
        return result
    }

    function displayStatus(status) {
        if (status === "Healthy") return "Confirmed"
        if (status === "Owner unavailable") return "Needs attention"
        return status || "Unknown"
    }

    function lastSyncLabel(timestamp) {
        var value = Number(timestamp || 0)
        if (value <= 0) return "Never"
        var minutes = Math.max(0, Math.floor((Date.now() - value) / 60000))
        if (minutes < 1) return "Just now"
        if (minutes < 60) return minutes + " min ago"
        var hours = Math.floor(minutes / 60)
        if (hours < 24) return hours + " h ago"
        return Math.floor(hours / 24) + " d ago"
    }

    function aggregateSummary() {
        if (connectedCount === 0)
            return attentionProviderCount > 0
                   ? "Disconnected third-party tracker work needs attention"
                   : "No third-party trackers connected"
        var parts = [connectedCount + (connectedCount === 1 ? " connected" : " connected")]
        if (waitingCount > 0) parts.push(waitingCount + " waiting")
        if (attentionProviderCount > 0)
            parts.push(attentionProviderCount + " need attention")
        else if (unresolvedCount > 0)
            parts.push(unresolvedCount + " need review")
        if (syncing) parts.unshift("Syncing")
        return parts.join(" · ")
    }

    function settingState(value) {
        return value === true ? "On" : "Off"
    }

    function importReviewsForSelectedProvider() {
        if (!trackerModel || selectedProviderKey === "global")
            return []
        var all = trackerModel.importReviews || []
        var matches = []
        for (var i = 0; i < all.length; ++i) {
            if (String(all[i].providerKey) === selectedProviderKey)
                matches.push(all[i])
        }
        return matches
    }

    function findItemByObjectName(parentItem, targetName) {
        if (!parentItem || !targetName)
            return null
        if (parentItem.objectName === targetName)
            return parentItem
        var children = parentItem.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findItemByObjectName(children[i], targetName)
            if (found)
                return found
        }
        return null
    }

    function importBatchSummary(batch) {
        if (batch.pageComplete === false)
            return "Waiting for the remaining tracker pages"
        var parts = []
        var decisions = Number(batch.decisionCount || 0)
        var awaitingApply = Number(batch.awaitingApplyCount || 0)
        var unresolved = Number(batch.unresolvedCount || 0)
        if (decisions > 0)
            parts.push(decisions + (decisions === 1 ? " item needs a decision" : " items need a decision"))
        if (unresolved > 0)
            parts.push(unresolved + (unresolved === 1 ? " item left unchanged" : " items left unchanged"))
        if (batch.confirmed === true) {
            if (awaitingApply > 0)
                parts.push(awaitingApply + (awaitingApply === 1
                            ? " approved item waiting for Colosseum"
                            : " approved items waiting for Colosseum"))
            else
                parts.push("Import review complete")
        } else if (decisions === 0) {
            parts.push("Ready to confirm")
        }
        return parts.join(" · ")
    }

    function openImportReview(batchId, invoker) {
        if (!trackerModel || !batchId)
            return false
        var focusTarget = invoker || (root.Window.window ? root.Window.window.activeFocusItem : null)
        importReviewInvokerObjectName = focusTarget ? String(focusTarget.objectName || "") : ""
        selectedImportBatchId = String(batchId)
        selectedBulkImportItemIds = []
        importReviewNotice = ""
        var opened = refreshImportReview()
        Qt.callLater(function() {
            if (root.importReviewOpen)
                importReviewCloseButton.forceActiveFocus(Qt.TabFocusReason)
        })
        return opened
    }

    function refreshImportReview() {
        if (!trackerModel || !selectedImportBatchId)
            return false
        var snapshot = trackerModel.importReviewSnapshot(selectedImportBatchId,
                                                          trackerModel.revision)
        selectedImportSnapshot = snapshot || ({ accepted: false, code: "owner_unavailable", items: [] })
        var snapshotRevision = Number(selectedImportSnapshot.revision)
        if (selectedImportSnapshot.accepted !== true
                || !isFinite(snapshotRevision) || snapshotRevision < 0) {
            importReviewNotice = selectedImportSnapshot.code === "connection_changed"
                                 ? "This review belongs to an older connection. No changes were applied."
                                 : "This import review is unavailable or has no current revision. No changes were applied."
            return false
        }
        return true
    }

    function importReviewRevision() {
        var revision = Number(selectedImportSnapshot.revision)
        return isFinite(revision) && revision >= 0 ? revision : -1
    }

    function importReviewIsCurrent() {
        return trackerModel && importReviewOpen
                && selectedImportSnapshot.accepted === true
                && importReviewRevision() >= 0
                && importReviewRevision() === Number(trackerModel.revision)
    }

    function closeImportReview() {
        if (titleMatchOpen) {
            closeTitleMatchFinder()
            return
        }
        if (bulkImportReviewOpen) {
            closeBulkImportReview()
            return
        }
        var invokerName = importReviewInvokerObjectName
        selectedImportBatchId = ""
        selectedImportSnapshot = ({ accepted: false, items: [] })
        selectedBulkImportItemIds = []
        importReviewInvokerObjectName = ""
        if (invokerName.length > 0)
            Qt.callLater(function() {
                var invoker = root.findItemByObjectName(root, invokerName)
                if (invoker && invoker.visible && invoker.enabled)
                    invoker.forceActiveFocus(Qt.BacktabFocusReason)
                else
                    root.takeKeyboardFocus()
            })
        else
            Qt.callLater(root.takeKeyboardFocus)
    }

    function openTitleMatchFinder(batchId, reviewItemId, expectedRevision) {
        if (!trackerModel || !importReviewOpen || batchId !== selectedImportBatchId
                || !importReviewIsCurrent()
                || Number(expectedRevision) !== importReviewRevision()
                || typeof trackerModel.titleMatchCandidates !== "function")
            return false
        var rows = selectedImportSnapshot.items || []
        var remoteTitle = ""
        for (var i = 0; i < rows.length; ++i) {
            if (String(rows[i].reviewItemId) === String(reviewItemId)) {
                remoteTitle = String(rows[i].title || "Untitled tracker item")
                break
            }
        }
        if (!remoteTitle.length)
            return false
        titleMatchBatchId = String(batchId)
        titleMatchReviewItemId = String(reviewItemId)
        titleMatchRemoteTitle = remoteTitle
        titleMatchSearchText = ""
        titleMatchNotice = ""
        titleMatchInvokerObjectName = "trackerImportChoice_"
                + titleMatchReviewItemId + "_find_match"
        titleMatchModel = trackerModel
        titleMatchRevision = importReviewRevision()
        titleMatchRequiresReopen = false
        titleMatchOpen = true
        refreshTitleMatchCandidates()
        Qt.callLater(function() {
            if (root.titleMatchOpen)
                titleMatchSearchField.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function refreshTitleMatchCandidates() {
        if (!titleMatchOpen || !trackerModel
                || typeof trackerModel.titleMatchCandidates !== "function") {
            titleMatchRows = []
            return false
        }
        if (titleMatchRequiresReopen) {
            titleMatchRows = []
            return false
        }
        if (!titleMatchReviewIsCurrent()) {
            titleMatchRows = []
            titleMatchNotice = "The preview changed. Close this search and reopen Find match from the current review."
            return false
        }
        titleMatchRows = trackerModel.titleMatchCandidates(
                    titleMatchBatchId, titleMatchReviewItemId,
                    titleMatchSearchText, titleMatchRevision) || []
        return true
    }

    function titleMatchReviewIsCurrent() {
        return titleMatchOpen && trackerModel && trackerModel === titleMatchModel
                && importReviewOpen && titleMatchBatchId === selectedImportBatchId
                && Number(titleMatchRevision) >= 0
                && Number(titleMatchRevision) === importReviewRevision()
                && Number(titleMatchRevision) === Number(trackerModel.revision)
    }

    function closeTitleMatchFinder(returnToInvoker) {
        if (!titleMatchOpen)
            return false
        var invokerName = titleMatchInvokerObjectName
        titleMatchOpen = false
        titleMatchRows = []
        titleMatchBatchId = ""
        titleMatchReviewItemId = ""
        titleMatchRemoteTitle = ""
        titleMatchSearchText = ""
        titleMatchInvokerObjectName = ""
        titleMatchModel = null
        titleMatchRevision = -1
        titleMatchRequiresReopen = false
        if (returnToInvoker !== false) {
            Qt.callLater(function() {
                var invoker = root.findItemByObjectName(root, invokerName)
                if (invoker && invoker.visible && invoker.enabled)
                    invoker.forceActiveFocus(Qt.BacktabFocusReason)
                else if (root.importReviewOpen && root.importReviewCanConfirm())
                    confirmImportButton.forceActiveFocus(Qt.TabFocusReason)
                else if (root.importReviewOpen)
                    importReviewCloseButton.forceActiveFocus(Qt.TabFocusReason)
            })
        }
        return true
    }

    function confirmTitleMatchCandidate(candidateId) {
        if (!trackerModel || !titleMatchOpen || titleMatchRequiresReopen)
            return false
        if (!titleMatchReviewIsCurrent()) {
            titleMatchNotice = "The preview changed. Reopen Find match from the current review before saving."
            closeTitleMatchFinder()
            trackerModel.refresh()
            refreshImportReview()
            importReviewNotice = "The preview changed. Inspect the current review before choosing a title match."
            return false
        }
        titleMatchChoicePending = true
        var accepted = trackerModel.confirmTitleMatch(
                    titleMatchBatchId, titleMatchReviewItemId,
                    String(candidateId), titleMatchRevision)
        titleMatchChoicePending = false
        if (!accepted) {
            var actionResult = trackerModel.lastActionResult || ({})
            if (actionResult.code === "stale_intent") {
                closeTitleMatchFinder()
                trackerModel.refresh()
                refreshImportReview()
                importReviewNotice = "The preview changed. Inspect the current review before choosing a title match."
                return false
            }
            if (actionResult.code === "preview_refresh_failed_mapping_retained") {
                titleMatchRequiresReopen = true
                titleMatchNotice = "The match is saved, but the review could not refresh. Progress and History were not changed. Reopen the review before choosing another match."
            } else if (actionResult.code === "preview_refresh_failed_mapping_rolled_back") {
                titleMatchNotice = "The match could not be saved because the review failed to refresh. No mapping was kept. Try again."
            } else if (actionResult.code === "preview_refresh_failed") {
                titleMatchNotice = "The review could not refresh. Check it again before choosing a match."
            } else {
                titleMatchNotice = "That title list changed. Search again before saving a match."
            }
            if (actionResult.code === "preview_refresh_failed_mapping_retained") {
                titleMatchRows = []
                return false
            }
            refreshTitleMatchCandidates()
            return false
        }
        importReviewNotice = "Title match saved. This tracker item has no verified exact Progress target, so Colosseum Progress and History remain unchanged."
        trackerModel.refresh()
        refreshImportReview()
        closeTitleMatchFinder(false)
        Qt.callLater(function() {
            if (root.importReviewOpen && root.importReviewCanConfirm())
                confirmImportButton.forceActiveFocus(Qt.TabFocusReason)
            else if (root.importReviewOpen)
                importReviewCloseButton.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function hasUncertainDelivery() {
        if (Number(selectedDossier.unknownOutcomeCount || 0) > 0)
            return true
        var rows = selectedDossier.deliveryRows || []
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].state === "checking_delivery" || rows[i].state === "syncing")
                return true
        }
        return false
    }

    function disconnectCleanupMode() {
        return selectedDossier.connected !== true
                && selectedDossier.cleanupEnabled === true
    }

    function importedDataReviewIsCurrent() {
        if (!trackerModel || !importedDataReviewOpen
                || trackerModel !== importedDataReviewModel
                || selectedProviderKey !== importedDataReviewProviderKey)
            return false
        var currentRevision = Number(trackerModel.revision)
        return importedDataReviewOpen
                && currentRevision === importedDataReviewRevision
                && String(selectedDossier.accountLabel || "") === importedDataReviewAccountLabel
    }

    function disconnectReviewIsCurrent() {
        if (!trackerModel || !disconnectReviewOpen
                || trackerModel !== disconnectReviewModel
                || selectedProviderKey !== disconnectReviewProviderKey)
            return false
        var currentRevision = Number(trackerModel.revision)
        return disconnectReviewOpen
                && currentRevision === disconnectReviewRevision
                && String(selectedDossier.accountLabel || "") === disconnectReviewAccountLabel
    }

    function invalidateImportedDataReview() {
        if (!importedDataReviewOpen)
            return false
        closeImportedDataReview()
        actionNotice = "This tracker's account or imported data changed while the review was open. Open Remove imported data again to review the current entries."
        return true
    }

    function invalidateDisconnectReview() {
        if (!disconnectReviewOpen)
            return false
        closeDisconnectReview()
        actionNotice = "This tracker's account or pending work changed while the review was open. Open Disconnect again to review the current state."
        return true
    }

    function openImportedDataReview() {
        if (!trackerModel)
            return false
        trackerModel.refresh()
        refreshDossier()
        if (selectedDossier.removeImportedEnabled !== true
                || (Number(selectedDossier.importedHistoryCount || 0) <= 0
                    && Number(selectedDossier.importedProgressCount || 0) <= 0))
            return false
        importedDataReviewNotice = ""
        importedRemovalAwaitingResult = false
        importedDataReviewModel = trackerModel
        importedDataReviewRevision = Number(trackerModel.revision)
        importedDataReviewProviderKey = selectedProviderKey
        importedDataReviewAccountLabel = String(selectedDossier.accountLabel || "")
        importedDataReviewOpen = true
        Qt.callLater(function() {
            if (root.importedDataReviewOpen
                    && root.importedDataReviewModel === root.trackerModel
                    && root.importedDataReviewProviderKey === root.selectedProviderKey)
                removeImportedCancelButton.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function closeImportedDataReview() {
        importedDataReviewOpen = false
        importedDataReviewNotice = ""
        importedDataReviewModel = null
        importedDataReviewRevision = -1
        importedDataReviewProviderKey = ""
        importedDataReviewAccountLabel = ""
        Qt.callLater(function() {
            if (removeImportedButton.visible && removeImportedButton.enabled)
                removeImportedButton.forceActiveFocus(Qt.BacktabFocusReason)
            else
                dossierCloseButton.forceActiveFocus(Qt.BacktabFocusReason)
        })
    }

    function completeImportedDataRemoval(result) {
        importedRemovalAwaitingResult = false
        var code = String(result.code || "unknown")
        if (code === "stale_intent") {
            invalidateImportedDataReview()
            return false
        }
        if (code === "removed") {
            closeImportedDataReview()
            actionNotice = "This tracker's imported History evidence and eligible Progress were removed. Native History, Activity, statistics, local Progress, and other providers' data were preserved."
            Qt.callLater(function() {
                if (root.dossierOpen)
                    dossierCloseButton.forceActiveFocus(Qt.TabFocusReason)
            })
            return true
        }

        var message = code === "progress_removed_history_removal_failed"
                ? "Eligible tracker-imported Progress was removed, but its History evidence remains. Reopen Remove imported data to review the remaining entries before retrying. Native History, Activity, statistics, local Progress, and other providers' data are preserved."
                : (code === "progress_removal_failed"
                   ? "Tracker-imported Progress could not be removed. Reopen Remove imported data to review the current entries before retrying. Tracker History evidence and all native data remain unchanged."
                   : "Colosseum could not finish removing this tracker's imported data. Reopen Remove imported data to review the current entries before retrying. Native History, Activity, statistics, local Progress, and other providers' data are preserved.")
        closeImportedDataReview()
        actionNotice = message
        return false
    }

    function confirmRemoveImportedData() {
        if (!trackerModel || !importedDataReviewOpen)
            return false
        if (!importedDataReviewIsCurrent()) {
            if (importedDataReviewOpen)
                invalidateImportedDataReview()
            return false
        }
        importedRemovalAwaitingResult = true
        var accepted = trackerModel.removeImportedData(importedDataReviewProviderKey,
                                                          importedDataReviewRevision)
        var result = trackerModel.lastActionResult || ({})
        trackerModel.refresh()
        refreshDossier()
        if (!accepted) {
            if (importedRemovalAwaitingResult)
                completeImportedDataRemoval(result)
            return false
        }
        if (result.code === "removal_pending")
            importedDataReviewNotice = "Removing this tracker's imported Progress and History evidence. This review will stay open until Colosseum confirms the result."
        else if (importedRemovalAwaitingResult)
            completeImportedDataRemoval(result)
        return true
    }

    function openDisconnectReview() {
        if (!trackerModel)
            return false
        trackerModel.refresh()
        refreshDossier()
        var cleanupMode = disconnectCleanupMode()
        if (!cleanupMode && selectedDossier.disconnectEnabled !== true)
            return false
        disconnectReviewNotice = ""
        disconnectReviewModel = trackerModel
        disconnectReviewRevision = Number(trackerModel.revision)
        disconnectReviewProviderKey = selectedProviderKey
        disconnectReviewAccountLabel = String(selectedDossier.accountLabel || "")
        disconnectReviewOpen = true
        Qt.callLater(function() {
            if (root.disconnectReviewOpen
                    && root.disconnectReviewModel === root.trackerModel
                    && root.disconnectReviewProviderKey === root.selectedProviderKey) {
                if (root.disconnectCleanupMode())
                    disconnectDiscardButton.forceActiveFocus(Qt.TabFocusReason)
                else
                    disconnectKeepPausedButton.forceActiveFocus(Qt.TabFocusReason)
            }
        })
        return true
    }

    function handleDisconnectReviewTab(event, from) {
        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab)
            return
        var reverse = event.key === Qt.Key_Backtab
                      || (event.modifiers & Qt.ShiftModifier)
        var order = [disconnectKeepPausedButton, disconnectDiscardButton,
                     disconnectCancelButton]
        var index = order.indexOf(from)
        if (index < 0)
            return
        order[(index + (reverse ? 2 : 1)) % order.length]
                .forceActiveFocus(reverse ? Qt.BacktabFocusReason : Qt.TabFocusReason)
        event.accepted = true
    }

    function closeDisconnectReview() {
        disconnectReviewOpen = false
        disconnectReviewNotice = ""
        disconnectActionPending = false
        disconnectReviewModel = null
        disconnectReviewRevision = -1
        disconnectReviewProviderKey = ""
        disconnectReviewAccountLabel = ""
        Qt.callLater(function() {
            if (disconnectButton.visible && disconnectButton.enabled)
                disconnectButton.forceActiveFocus(Qt.BacktabFocusReason)
            else
                root.takeKeyboardFocus()
        })
    }

    function confirmDisconnect(choice) {
        if (!trackerModel || !disconnectReviewOpen)
            return false
        if (!disconnectReviewIsCurrent()) {
            if (disconnectReviewOpen)
                invalidateDisconnectReview()
            return false
        }
        var wasConnected = selectedDossier.connected === true
        if (!wasConnected && choice === "keep_paused") {
            closeDisconnectReview()
            actionNotice = "The tracker remains disconnected. Known-unsent work stays paused for recovery."
            return true
        }
        disconnectActionPending = true
        var accepted = trackerModel.disconnectTracker(disconnectReviewProviderKey, choice,
                                                       disconnectReviewRevision)
        disconnectActionPending = false
        trackerModel.refresh()
        refreshDossier()
        if (!accepted) {
            var resultCode = trackerModel.lastActionResult
                             ? String(trackerModel.lastActionResult.code || "") : ""
            if (resultCode === "stale_intent") {
                invalidateDisconnectReview()
                return false
            } else if (resultCode === "delivery_in_flight") {
                disconnectReviewNotice = "A tracker update is still sending. Let it finish, then review its result before disconnecting."
            } else if (resultCode === "disconnect_cleanup_pending") {
                disconnectReviewNotice = "The tracker is disconnected, but known-unsent updates remain paused. Retry cleanup here; uncertain outcomes were kept."
            } else {
                disconnectReviewNotice = "Colosseum could not complete the disconnect. Check this tracker's connection and pending work before trying again."
            }
            return false
        }
        closeDisconnectReview()
        if (wasConnected) {
            actionNotice = choice === "discard_known_unsent"
                           ? "Disconnected. Known-unsent tracker updates were discarded; uncertain deliveries were kept for recovery."
                           : "Disconnected. Pending tracker work remains paused for recovery."
        } else {
            actionNotice = "Known-unsent updates were removed. Uncertain outcomes remain paused for recovery after reconnect."
        }
        Qt.callLater(function() { dossierCloseButton.forceActiveFocus(Qt.TabFocusReason) })
        return true
    }

    function importChoiceLabel(choice) {
        if (choice === "use_provider_progress") return "Use tracker progress"
        if (choice === "keep_colosseum") return "Keep Colosseum progress"
        if (choice === "leave_unmatched") return "Leave unmatched"
        if (choice === "leave_unresolved") return "Leave for later"
        if (choice === "find_match") return "Find match"
        return "Unavailable"
    }

    function bulkImportSelectionContains(itemId) {
        var selected = selectedBulkImportItemIds || []
        return selected.indexOf(String(itemId)) >= 0
    }

    function toggleBulkImportItem(itemId) {
        var key = String(itemId)
        var selected = (selectedBulkImportItemIds || []).slice()
        var index = selected.indexOf(key)
        if (index >= 0)
            selected.splice(index, 1)
        else
            selected.push(key)
        selectedBulkImportItemIds = selected
        return true
    }

    function bulkImportCommonChoices() {
        var ids = selectedBulkImportItemIds || []
        if (ids.length < 2)
            return []
        var rows = selectedImportSnapshot.items || []
        var common = null
        for (var i = 0; i < ids.length; ++i) {
            var row = null
            for (var r = 0; r < rows.length; ++r) {
                if (String(rows[r].reviewItemId) === String(ids[i])) {
                    row = rows[r]
                    break
                }
            }
            if (!row || row.state !== "review_required")
                return []
            var allowed = (row.allowedChoices || []).filter(function(choice) {
                return choice !== "find_match"
            })
            common = common === null ? allowed : common.filter(function(choice) {
                return allowed.indexOf(choice) >= 0
            })
            if (common.length === 0)
                return []
        }
        return common || []
    }

    function selectedBulkImportTitles() {
        var ids = selectedBulkImportItemIds || []
        var rows = selectedImportSnapshot.items || []
        var titles = []
        for (var i = 0; i < ids.length; ++i) {
            for (var r = 0; r < rows.length; ++r) {
                if (String(rows[r].reviewItemId) === String(ids[i])) {
                    titles.push(String(rows[r].title || "Untitled entry"))
                    break
                }
            }
        }
        return titles
    }

    function openBulkImportReview() {
        if (!trackerModel || !importReviewIsCurrent()
                || selectedBulkImportItemIds.length < 2
                || bulkImportCommonChoices().length === 0)
            return false
        bulkImportReviewNotice = ""
        bulkImportReviewModel = trackerModel
        bulkImportReviewRevision = importReviewRevision()
        bulkImportReviewBatchId = selectedImportBatchId
        bulkImportReviewItemIds = (selectedBulkImportItemIds || []).slice()
        bulkImportReviewOpen = true
        Qt.callLater(function() {
            if (root.bulkImportReviewOpen)
                bulkImportReviewCancelButton.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function closeBulkImportReview() {
        bulkImportReviewOpen = false
        bulkImportReviewNotice = ""
        bulkImportReviewModel = null
        bulkImportReviewRevision = -1
        bulkImportReviewBatchId = ""
        bulkImportReviewItemIds = []
        Qt.callLater(function() {
            var reviewButton = root.findItemByObjectName(root, "trackerImportBulkReviewButton")
            if (root.importReviewOpen && reviewButton && reviewButton.visible
                    && reviewButton.enabled)
                reviewButton.forceActiveFocus(Qt.BacktabFocusReason)
            else if (root.importReviewOpen)
                cancelImportReviewButton.forceActiveFocus(Qt.TabFocusReason)
        })
    }

    function bulkImportReviewIsCurrent() {
        if (!bulkImportReviewOpen || !trackerModel
                || trackerModel !== bulkImportReviewModel
                || selectedImportBatchId !== bulkImportReviewBatchId
                || !importReviewIsCurrent()
                || Number(bulkImportReviewRevision) < 0
                || Number(bulkImportReviewRevision) !== Number(trackerModel.revision)
                || Number(bulkImportReviewRevision) !== importReviewRevision())
            return false
        var selected = selectedBulkImportItemIds || []
        var reviewed = bulkImportReviewItemIds || []
        if (selected.length !== reviewed.length)
            return false
        for (var i = 0; i < reviewed.length; ++i) {
            if (String(selected[i]) !== String(reviewed[i]))
                return false
        }
        return true
    }

    function invalidateBulkImportReview(message) {
        if (!bulkImportReviewOpen)
            return false
        closeBulkImportReview()
        selectedBulkImportItemIds = []
        importReviewNotice = message
        if (trackerModel && importReviewOpen) {
            trackerModel.refresh()
            refreshImportReview()
        }
        return true
    }

    function confirmBulkImportChoice(choice) {
        if (!trackerModel || !bulkImportReviewOpen)
            return false
        if (!bulkImportReviewIsCurrent()) {
            invalidateBulkImportReview("The review changed. Inspect the current titles and select them again before making a group decision.")
            return false
        }
        var batchId = bulkImportReviewBatchId
        var expectedRevision = Number(bulkImportReviewRevision)
        var ids = (bulkImportReviewItemIds || []).slice()
        if (bulkImportCommonChoices().indexOf(choice) < 0) {
            bulkImportReviewNotice = "That choice is not available for every selected item. No decisions were saved."
            return false
        }
        bulkImportChoicePending = true
        var accepted = trackerModel.resolveImportItems(batchId, ids, choice,
                                                        expectedRevision)
        bulkImportChoicePending = false
        if (!accepted) {
            var result = trackerModel.lastActionResult || ({})
            if (result.code === "stale_intent"
                    || Number(trackerModel.revision) !== expectedRevision) {
                invalidateBulkImportReview("The review changed. Inspect the current titles and select them again before making a group decision.")
            } else {
                bulkImportReviewNotice = "These items changed while you were reviewing. No bulk decision was saved; refresh and inspect them again."
                trackerModel.refresh()
                refreshImportReview()
            }
            return false
        }
        trackerModel.refresh()
        refreshImportReview()
        closeBulkImportReview()
        selectedBulkImportItemIds = []
        importReviewNotice = "The selected decisions were saved in this preview. Nothing was applied before import confirmation."
        Qt.callLater(function() {
            if (root.importReviewOpen)
                cancelImportReviewButton.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function importClassificationLabel(classification) {
        if (classification === "new_progress") return "New progress"
        if (classification === "exact_match") return "Already matches"
        if (classification === "remote_advance") return "Tracker is ahead"
        if (classification === "disagreement") return "Progress differs"
        if (classification === "needs_matching") return "Needs a title match"
        if (classification === "unsupported") return "Not supported"
        if (classification === "duplicate") return "Duplicate entry"
        return "Needs review"
    }

    function importProgressSummary(item) {
        var provider = item.providerCompleted ? "Completed" : "Episode " + item.providerProgress
        if (item.hasLocalAtPreview !== true)
            return "Tracker: " + provider + " · No Colosseum progress yet"
        var local = item.localCompleted ? "Completed" : "Episode " + item.localProgress
        return "Tracker: " + provider + " · Colosseum: " + local
    }

    function deliveryStateLabel(state) {
        if (state === "waiting") return "Waiting to sync"
        if (state === "syncing") return "Syncing"
        if (state === "retrying") return "Retry scheduled"
        if (state === "checking_delivery") return "Checking delivery"
        if (state === "needs_attention") return "Needs attention"
        if (state === "failed") return "Could not deliver"
        return "Delivery status unknown"
    }

    function deliveryReasonDetail(reason) {
        if (reason === "none")
            return "Saved in Colosseum. Waiting for the tracker to sync."
        if (reason === "acknowledgement_lost" || reason === "delivery_uncertain")
            return "The provider may have received this. Colosseum is checking before any retry."
        if (reason === "authentication_required")
            return "Reconnect this tracker before Colosseum resumes delivery."
        if (reason === "unsupported_action")
            return "This tracker does not support this action. Colosseum keeps its own progress."
        if (reason === "provider_unavailable" || reason === "provider_retry")
            return "Colosseum saved the change locally and is waiting for the tracker."
        if (reason === "rate_limited")
            return "The tracker asked Colosse to wait before continuing."
        if (reason === "local_state_changed")
            return "Colosseum progress changed while this update was waiting. Review is needed."
        if (reason === "title_match_changed")
            return "The title match changed while this update was waiting. Review is needed."
        if (reason === "destination_review_required")
            return "This update came from another profile. Review it in this profile before sending."
        return "Colosseum has kept this update for recovery."
    }

    function resolveImportChoice(item, choice) {
        if (!trackerModel || !selectedImportSnapshot.accepted)
            return false
        if (!importReviewIsCurrent()) {
            importReviewNotice = "The preview changed. Inspect the current item state before choosing again."
            trackerModel.refresh()
            refreshImportReview()
            return false
        }
        var resolvedItemId = String(item.reviewItemId)
        var expectedRevision = importReviewRevision()
        var accepted = trackerModel.resolveImportItem(selectedImportBatchId,
                                                        resolvedItemId,
                                                        choice,
                                                        expectedRevision)
        importReviewNotice = accepted
                ? (choice === "find_match"
                   ? "Choose a Colosseum Progress title. A title match alone will not change Progress or History."
                   : "Choice saved in this preview.")
                : "That choice is no longer available. Review the latest state."
        trackerModel.refresh()
        refreshImportReview()
        if (accepted)
            selectedBulkImportItemIds = []
        if (accepted && choice !== "find_match")
            Qt.callLater(function() { root.restoreImportReviewFocusAfterDecision(resolvedItemId) })
        return accepted
    }

    function restoreImportReviewFocusAfterDecision(resolvedItemId) {
        if (!importReviewOpen)
            return false
        var rows = selectedImportSnapshot.items || []
        var startIndex = -1
        for (var i = 0; i < rows.length; ++i) {
            if (String(rows[i].reviewItemId) === resolvedItemId) {
                startIndex = i
                break
            }
        }
        for (var step = 1; step <= rows.length; ++step) {
            var candidateIndex = startIndex < 0 ? step - 1 : (startIndex + step) % rows.length
            if (candidateIndex < 0 || candidateIndex >= rows.length
                || !(rows[candidateIndex].allowedChoices || []).length)
                continue
            importReviewList.positionViewAtIndex(candidateIndex, ListView.Contain)
            Qt.callLater(function() {
                if (!root.importReviewOpen)
                    return
                var card = importReviewList.itemAtIndex(candidateIndex)
                if (card && card.firstDecisionButton
                    && card.firstDecisionButton.visible && card.firstDecisionButton.enabled) {
                    card.firstDecisionButton.forceActiveFocus(Qt.TabFocusReason)
                } else if (root.importReviewCanConfirm()) {
                    confirmImportButton.forceActiveFocus(Qt.TabFocusReason)
                } else {
                    cancelImportReviewButton.forceActiveFocus(Qt.TabFocusReason)
                }
            })
            return true
        }
        if (importReviewCanConfirm())
            confirmImportButton.forceActiveFocus(Qt.TabFocusReason)
        else
            cancelImportReviewButton.forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    function importReviewCanConfirm() {
        if (!importReviewIsCurrent()
            || selectedImportSnapshot.pageComplete !== true
            || selectedImportSnapshot.confirmed === true)
            return false
        var rows = selectedImportSnapshot.items || []
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].state === "review_required" || rows[i].state === "applying")
                return false
        }
        return true
    }

    function confirmImportReview() {
        if (!trackerModel || !importReviewCanConfirm()) {
            if (trackerModel && importReviewOpen && !importReviewIsCurrent()) {
                importReviewNotice = "The preview changed. Inspect the current state before confirming."
                trackerModel.refresh()
                refreshImportReview()
            }
            return false
        }
        var expectedRevision = importReviewRevision()
        var accepted = trackerModel.confirmImport(selectedImportBatchId,
                                                   expectedRevision)
        var resultCode = trackerModel.lastActionResult
                         ? String(trackerModel.lastActionResult.code || "") : ""
        importReviewNotice = accepted
                             ? "Review confirmed. Colosseum History and Activity remain native-owned."
                             : resultCode === "review_incomplete"
                               ? "This provider has more pages to read. Colosseum has not confirmed the import."
                               : resultCode === "stale_intent"
                                 ? "The preview changed. Review the latest state before confirming."
                                 : "Colosseum could not confirm this review. No History or Activity was changed."
        trackerModel.refresh()
        refreshImportReview()
        if (accepted)
            Qt.callLater(function() {
                if (root.importReviewOpen)
                    cancelImportReviewButton.forceActiveFocus(Qt.TabFocusReason)
            })
        return accepted
    }

    function handleImportReviewKeys(event) {
        if (event.key === Qt.Key_Escape) {
            if (titleMatchOpen)
                closeTitleMatchFinder()
            else
                closeImportReview()
            event.accepted = true
        }
    }

    function saveGlobalSetting(key, enabled) {
        if (!trackerModel || !selectedDossier.editable)
            return false
        var accepted = trackerModel.setGlobalSetting(key, enabled, trackerModel.revision)
        trackerModel.refresh()
        refreshDossier()
        actionNotice = accepted ? "Preference saved in Colosseum."
                                : "Colosseum could not save that preference."
        return accepted
    }

    function saveProviderSetting(setting, enabled) {
        if (!trackerModel || !selectedDossier.connected)
            return false
        var expectedRevision = trackerModel.revision
        var accepted = false
        if (setting === "pull")
            accepted = trackerModel.setProviderPullAutomatically(selectedProviderKey,
                                                                  enabled,
                                                                  expectedRevision)
        else if (setting === "send")
            accepted = trackerModel.setProviderSendEnabled(selectedProviderKey,
                                                            enabled,
                                                            expectedRevision)
        else if (setting === "live")
            accepted = trackerModel.setLivePlaybackTrackingEnabled(selectedProviderKey,
                                                                    enabled,
                                                                    expectedRevision)
        trackerModel.refresh()
        refreshDossier()
        actionNotice = accepted ? "Tracker preference saved."
                                : "That tracker preference is unavailable."
        return accepted
    }

    function openDossier(providerKey, invoker) {
        if (!trackerModel || !providerKey) return false
        dossierInvoker = invoker || (root.Window.window ? root.Window.window.activeFocusItem : null)
        selectedProviderKey = String(providerKey)
        recoveryRouteGeneration++
        recoveryRouteActive = false
        recoveryRouteProviderKey = ""
        recoveryRouteFocusIndex = -1
        recoveryRouteRevision = -1
        recoveryRouteState = ""
        recoveryRouteReason = ""
        recoveryRouteModel = null
        actionNotice = ""
        refreshDossier()
        var openingProviderKey = selectedProviderKey
        Qt.callLater(function() {
            if (root.dossierOpen
                    && root.selectedProviderKey === openingProviderKey
                    && !root.recoveryRouteActive
                    && !root.importReviewOpen
                    && !root.exportReviewOpen
                    && !root.titleMatchOpen
                    && !root.bulkImportReviewOpen
                    && !root.disconnectReviewOpen
                    && !root.importedDataReviewOpen) {
                dossierCloseButton.forceActiveFocus(Qt.TabFocusReason)
            }
        })
        return true
    }

    function openRecoveryRoute(providerKey, invoker) {
        if (!trackerModel || typeof trackerModel.diagnoseRoute !== "function")
            return openDossier(providerKey, invoker)

        var requestedProviderKey = String(providerKey)
        var route = trackerModel.diagnoseRoute(requestedProviderKey)
        var focusIndex = route ? Number(route.focusIndex) : -1
        var routeRevision = route ? Number(route.revision) : -1
        var routeState = route ? String(route.state || "") : ""
        var routeReason = route ? String(route.reason || "") : ""
        var routeIsCurrentAndSafe = route && route.accepted === true
                && route.providerKey === requestedProviderKey
                && route.destination === "provider_dossier"
                && route.focusTarget === "delivery_status"
                && isFinite(focusIndex) && focusIndex >= 0
                && Math.floor(focusIndex) === focusIndex
                && isFinite(routeRevision) && routeRevision >= 0
                && Math.floor(routeRevision) === routeRevision
                && routeState.length > 0 && routeReason.length > 0
                && Number(trackerModel.revision) === routeRevision
        if (!routeIsCurrentAndSafe)
            return openDossier(providerKey, invoker)
        if (!openDossier(providerKey, invoker))
            return false
        // Loading the dossier reads the queue again. If that changed the model
        // revision, keep the dossier open but do not focus the stale row index.
        if (Number(trackerModel.revision) !== routeRevision)
            return true

        recoveryRouteActive = true
        recoveryRouteProviderKey = requestedProviderKey
        recoveryRouteFocusIndex = focusIndex
        recoveryRouteRevision = routeRevision
        recoveryRouteState = routeState
        recoveryRouteReason = routeReason
        recoveryRouteModel = trackerModel
        var routeGeneration = ++recoveryRouteGeneration
        Qt.callLater(function() {
            if (!root.dossierOpen
                    || root.selectedProviderKey !== root.recoveryRouteProviderKey
                    || !root.recoveryRouteActive
                    || root.recoveryRouteGeneration !== routeGeneration
                    || root.trackerModel !== root.recoveryRouteModel) {
                return
            }
            if (Number(root.trackerModel.revision)
                    !== Number(root.recoveryRouteRevision)) {
                root.abandonRecoveryRoute()
                return
            }
            var row = deliveryStatusRows.itemAt(root.recoveryRouteFocusIndex)
            if (!row || !row.visible || !row.enabled || !row.modelData
                    || String(row.modelData.state || "") !== root.recoveryRouteState
                    || String(row.modelData.reason || "") !== root.recoveryRouteReason) {
                root.abandonRecoveryRoute()
                return
            }
            var rowY = row.mapToItem(dossierScroll.contentItem, 0, 0).y
            var rowBottom = rowY + row.height
            if (rowY < dossierScroll.contentY) {
                dossierScroll.contentY = rowY
            } else if (rowBottom > dossierScroll.contentY + dossierScroll.height) {
                dossierScroll.contentY = rowBottom - dossierScroll.height
            }
            dossierScroll.contentY = Math.max(0, Math.min(dossierScroll.contentY,
                dossierScroll.contentHeight - dossierScroll.height))
            row.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function abandonRecoveryRoute() {
        recoveryRouteGeneration++
        recoveryRouteActive = false
        recoveryRouteProviderKey = ""
        recoveryRouteFocusIndex = -1
        recoveryRouteRevision = -1
        recoveryRouteState = ""
        recoveryRouteReason = ""
        recoveryRouteModel = null
        refreshDossier()
        if (dossierOpen)
            dossierCloseButton.forceActiveFocus(Qt.TabFocusReason)
    }

    function refreshDossier() {
        if (!dossierOpen || !trackerModel) return
        if (selectedProviderKey === "global") {
            selectedDossier = {
                found: true,
                providerKey: "global",
                providerName: "Connection preferences",
                status: "Colosseum first",
                profileAvailable: globalSettings.profileAvailable === true,
                editable: globalSettings.editable === true,
                trackerSyncEnabled: globalSettings.trackerSyncEnabled === true,
                checkOnLaunch: globalSettings.checkOnLaunch === true,
                backgroundDelivery: globalSettings.backgroundDelivery === true,
                completionMessages: globalSettings.completionMessages === true
            }
        } else {
            var dossier = trackerModel.providerDossier(selectedProviderKey)
            var deliveryRows = typeof trackerModel.deliveryRows === "function"
                               ? trackerModel.deliveryRows(selectedProviderKey) : []
            selectedDossier = Object.assign({}, dossier, { deliveryRows: deliveryRows })
        }
    }

    function closeDossier() {
        if (importReviewOpen)
            closeImportReview()
        if (exportReviewOpen)
            closeExportReview()
        var invoker = dossierInvoker
        selectedProviderKey = ""
        selectedDossier = ({})
        dossierInvoker = null
        recoveryRouteGeneration++
        recoveryRouteActive = false
        recoveryRouteProviderKey = ""
        recoveryRouteFocusIndex = -1
        recoveryRouteRevision = -1
        recoveryRouteState = ""
        recoveryRouteReason = ""
        recoveryRouteModel = null
        if (invoker && invoker.visible && invoker.enabled)
            Qt.callLater(function() { invoker.forceActiveFocus(Qt.BacktabFocusReason) })
        else
            Qt.callLater(root.takeKeyboardFocus)
    }

    // Returns true only when this Escape was consumed by the dossier. The host
    // then closes the full page on the next Escape through ShellBackPolicy.
    function requestEscape() {
        if (exportReviewOpen) {
            closeExportReview()
            return true
        }
        if (disconnectReviewOpen) {
            closeDisconnectReview()
            return true
        }
        if (importedDataReviewOpen) {
            closeImportedDataReview()
            return true
        }
        if (titleMatchOpen) {
            closeTitleMatchFinder()
            return true
        }
        if (bulkImportReviewOpen) {
            closeBulkImportReview()
            return true
        }
        if (importReviewOpen) {
            closeImportReview()
            return true
        }
        if (!dossierOpen) {
            backRequested()
            return false
        }
        closeDossier()
        return true
    }

    function takeKeyboardFocus() {
        preferencesButton.forceActiveFocus(Qt.TabFocusReason)
    }

    function focusMainSyncButton() {
        mainSyncButton.forceActiveFocus(Qt.TabFocusReason)
    }

    function syncNow() {
        if (!syncAllEnabled || !trackerModel) return false
        return trackerModel.requestSyncAll(trackerModel.revision)
    }

    function openExportReview() {
        if (!trackerModel || selectedDossier.exportReviewEnabled !== true
                || typeof trackerModel.beginExportReview !== "function")
            return false
        var review = trackerModel.beginExportReview(selectedProviderKey,
                                                   Number(trackerModel.revision))
        if (!review || review.accepted !== true) {
            actionNotice = "The send review could not open. Check the tracker connection and try again."
            return false
        }
        selectedExportReview = review
        selectedExportItemIds = []
        exportReviewModel = trackerModel
        exportReviewNotice = ""
        Qt.callLater(function() {
            if (root.exportReviewOpen)
                exportReviewCloseButton.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function closeExportReview() {
        selectedExportReview = ({ accepted: false, items: [] })
        selectedExportItemIds = []
        exportReviewModel = null
        exportConfirmationPending = false
        exportReviewNotice = ""
        if (dossierOpen)
            Qt.callLater(function() { exportReviewButton.forceActiveFocus(Qt.BacktabFocusReason) })
    }

    function exportReviewIsCurrent() {
        return exportReviewOpen && trackerModel === exportReviewModel
                && Number(selectedExportReview.revision) === Number(trackerModel.revision)
    }

    function toggleExportItem(itemId, eligible) {
        if (!eligible || !exportReviewIsCurrent())
            return
        var next = selectedExportItemIds.slice()
        var index = next.indexOf(itemId)
        if (index >= 0)
            next.splice(index, 1)
        else
            next.push(itemId)
        selectedExportItemIds = next
    }

    function confirmExportReview() {
        if (!exportReviewIsCurrent()) {
            exportReviewNotice = "The review changed. Open a fresh send review."
            return false
        }
        if (selectedExportItemIds.length === 0) {
            exportReviewNotice = "Select at least one local item to send."
            return false
        }
        exportConfirmationPending = true
        var accepted = trackerModel.confirmExportReview(selectedExportReview.reviewId,
            selectedExportItemIds, Number(selectedExportReview.revision))
        exportConfirmationPending = false
        if (!accepted) {
            closeExportReview()
            actionNotice = "The send review changed. Open it again before sending."
            return false
        }
        closeExportReview()
        refreshDossier()
        actionNotice = "Selected Colosseum updates are waiting to sync."
        return true
    }

    Connections {
        target: root.trackerModel
        ignoreUnknownSignals: true
        function onModelChanged() {
            root.refreshDossier()
            var currentRevision = root.trackerModel
                                  ? Number(root.trackerModel.revision) : -1
            if (root.exportReviewOpen && !root.exportConfirmationPending
                    && !root.exportReviewIsCurrent()) {
                root.closeExportReview()
                root.actionNotice = "The send review changed. Open it again before sending."
            }
            if (root.disconnectReviewOpen && !root.disconnectActionPending
                    && (root.trackerModel !== root.disconnectReviewModel
                        || root.selectedProviderKey !== root.disconnectReviewProviderKey
                        || currentRevision !== root.disconnectReviewRevision
                        || String(root.selectedDossier.accountLabel || "")
                           !== root.disconnectReviewAccountLabel))
                root.invalidateDisconnectReview()
            if (root.importedDataReviewOpen && !root.importedRemovalAwaitingResult
                    && (root.trackerModel !== root.importedDataReviewModel
                        || root.selectedProviderKey !== root.importedDataReviewProviderKey
                        || currentRevision !== root.importedDataReviewRevision
                        || String(root.selectedDossier.accountLabel || "")
                           !== root.importedDataReviewAccountLabel))
                root.invalidateImportedDataReview()
            if (root.titleMatchOpen && !root.titleMatchChoicePending
                    && !root.titleMatchReviewIsCurrent()) {
                root.closeTitleMatchFinder()
                root.importReviewNotice = "The preview changed. Inspect the current review before choosing a title match."
            }
            if (root.bulkImportReviewOpen && !root.bulkImportChoicePending
                    && !root.bulkImportReviewIsCurrent()) {
                root.invalidateBulkImportReview("The review changed. Inspect the current titles and select them again before making a group decision.")
            }
            if (root.importReviewOpen)
                Qt.callLater(root.refreshImportReview)
            var result = root.trackerModel ? root.trackerModel.lastActionResult || ({}) : ({})
            if (root.importedRemovalAwaitingResult
                    && result.action === "remove_imported_tracker_data"
                    && result.code !== "removal_pending"
                    && root.selectedDossier.removeImportedPending !== true)
                root.completeImportedDataRemoval(result)
        }
        function onFindMatchRequested(batchId, reviewItemId, revision) {
            if (!root.openTitleMatchFinder(batchId, reviewItemId, revision))
                root.importReviewNotice = "Title matching is unavailable for this tracker item. You can leave it unmatched."
        }
    }

    Item {
        anchors.fill: parent

        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }

        Rectangle {
            anchors.fill: parent
            color: theme.biblioWashBottom
            opacity: root.backdrop ? 0.82 : 1
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(12 / 255, 15 / 255, 24 / 255, 0.28) }
                GradientStop { position: 1; color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.45) }
            }
        }
    }

    Flickable {
        id: pageScroll
        objectName: "trackerSyncCenterScroll"
        anchors.fill: parent
        anchors.bottomMargin: root.taskbarBottomClearance
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: pageColumn.y + pageColumn.implicitHeight + 116
        activeFocusOnTab: true
        Keys.onPressed: (event) => pageSpatialNav.handle(event)
        Keys.onReleased: (event) => pageSpatialNav.handleRelease(event)
        ScrollBar.vertical: HouseScrollBar { flick: pageScroll }

        ColumnLayout {
            id: pageColumn
            x: root.pageInset
            y: 45
            width: root.width - root.pageInset * 2
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                spacing: 20

                ColumnLayout {
                    spacing: 0
                    Text {
                        objectName: "trackerSyncPageTitle"
                        text: "Connections"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 52
                        font.weight: Font.DemiBold
                        font.letterSpacing: -1.8
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        text: "Your external library connections"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: preferencesButton
                    objectName: "trackerPreferencesButton"
                    Accessible.name: "Connection preferences"
                    activeFocusOnTab: true
                    onClicked: root.openDossier("global", this)
                    contentItem: RowLayout {
                        spacing: 9
                        Image {
                            source: "../assets/icons/preferences.svg"
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            text: "Preferences"
                            color: preferencesButton.enabled ? theme.ink : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                    background: Rectangle {
                        radius: 12
                        color: preferencesButton.down ? theme.glassHi : theme.glassTint
                        border.width: 1
                        border.color: preferencesButton.activeFocus ? theme.gold : theme.edge
                    }
                    padding: 13
                }

                Button {
                    id: syncAllButton
                    objectName: "trackerSyncAllButton"
                    Accessible.name: "Sync all connected trackers"
                    activeFocusOnTab: true
                    enabled: root.syncAllEnabled
                    onClicked: root.syncNow()
                    contentItem: RowLayout {
                        spacing: 9
                        Image {
                            source: "../assets/icons/sync.svg"
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            fillMode: Image.PreserveAspectFit
                            opacity: syncAllButton.enabled ? 1 : 0.5
                        }
                        Text {
                            text: "Sync all"
                            color: syncAllButton.enabled ? theme.gold : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                    background: Rectangle {
                        radius: 12
                        color: syncAllButton.enabled ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.07)
                                                     : theme.glassTint
                        border.width: 1
                        border.color: syncAllButton.enabled ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42)
                                                            : theme.edge
                    }
                    padding: 13
                }
            }

            Text {
                visible: root.connectedCount === 0
                text: root.aggregateSummary()
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 11
                Layout.alignment: Qt.AlignRight
                Layout.bottomMargin: 10
            }

            Rectangle {
                objectName: "trackerEmptyState"
                Layout.fillWidth: true
                Layout.preferredHeight: 138
                Layout.bottomMargin: 16
                visible: root.connectedCount === 0
                radius: 17
                color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.96)
                border.width: 1
                border.color: theme.edge

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 30

                    Image {
                        objectName: "trackerNativeLibraryIcon"
                        source: "../assets/icons/colosseum.svg"
                        Layout.preferredWidth: root.width >= 900 ? 180 : 72
                        Layout.preferredHeight: 80
                        fillMode: Image.PreserveAspectFit
                    }
                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: theme.edge
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: "NATIVE LIBRARY"
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 10
                            font.letterSpacing: 1.7
                            font.weight: Font.Bold
                        }
                        Text {
                            text: "Colosseum"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 29
                        }
                        Text {
                            text: "Works without trackers."
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 15
                        }
                        Text {
                            text: "Your native progress, History, and activity stay here."
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    Rectangle {
                        visible: root.width >= 900
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 36
                        radius: 18
                        color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.05)
                        border.width: 1
                        border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.32)
                        Text {
                            anchors.centerIn: parent
                            text: "✓  Always available"
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Rectangle {
                objectName: "trackerSyncRelay"
                Layout.fillWidth: true
                Layout.bottomMargin: 16
                visible: root.connectedCount > 0
                implicitHeight: relayColumn.implicitHeight + (root.connectedCount > 0 ? 54 : 36)
                radius: 22
                color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.045)
                border.width: 1
                border.color: theme.edge

                ColumnLayout {
                    id: relayColumn
                    anchors.fill: parent
                    anchors.margins: root.connectedCount > 0 ? 28 : 18
                    spacing: root.connectedCount > 0 ? 20 : 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "CONNECTION STATUS"
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                            font.letterSpacing: 1.7
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                        }
                        Text {
                            id: relaySummary
                            objectName: "trackerRelayStatus"
                            text: root.aggregateSummary()
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    Item {
                        id: relayMap
                        objectName: "trackerRelayMap"
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.connectedCount > 0 ? 190 : 132

                        visible: root.connectedCount > 0

                        Rectangle {
                            id: canonicalNode
                            objectName: "trackerCanonicalNode"
                            width: 220
                            height: 76
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            radius: 18
                            color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.055)
                            border.width: 1
                            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.38)

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 15
                                Image {
                                    source: "../assets/icons/colosseum.svg"
                                    Layout.preferredWidth: 47
                                    Layout.preferredHeight: 47
                                    fillMode: Image.PreserveAspectFit
                                }
                                ColumnLayout {
                                    spacing: 5
                                    Text {
                                        text: "Colosseum"
                                        color: theme.ink
                                        font.family: theme.display
                                        font.pixelSize: 20
                                    }
                                    Text {
                                        text: "CANONICAL"
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        font.letterSpacing: 0.5
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: root.connectedCount > 0
                            x: parent.width / 2 - 0.5
                            y: 76
                            width: 1
                            height: 30
                            color: theme.edge
                        }
                        Rectangle {
                            visible: root.connectedCount > 1
                            x: parent.width * 0.17
                            y: 106
                            width: parent.width * 0.66
                            height: 1
                            color: theme.edge
                        }
                        Repeater {
                            model: root.connectedTrackers
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                x: (index + 0.5) * relayMap.width / root.connectedCount - 0.5
                                y: root.connectedCount > 1 ? 106 : 106
                                width: 1
                                height: 16
                                color: theme.edge
                            }
                        }

                        RowLayout {
                            id: relayProviderRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            y: 122
                            height: 68
                            spacing: 12

                            Repeater {
                                model: root.connectedTrackers
                                delegate: Rectangle {
                                    id: relayCard
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 68
                                    radius: 15
                                    color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.72)
                                    border.width: 1
                                    border.color: modelData.status === "Attention"
                                                  ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.52)
                                                  : theme.edge
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 11
                                        spacing: 12
                                        Rectangle {
                                            Layout.preferredWidth: 42
                                            Layout.preferredHeight: 42
                                            radius: 12
                                            color: theme.glassTint
                                            border.width: 1
                                            border.color: theme.edge
                                            Image {
                                                anchors.centerIn: parent
                                                source: root.providerIcon(relayCard.modelData.providerKey)
                                                width: 26; height: 26
                                                fillMode: Image.PreserveAspectFit
                                            }
                                        }
                                        ColumnLayout {
                                            spacing: 5
                                            Layout.fillWidth: true
                                            Text {
                                                text: relayCard.modelData.providerName || "Tracker"
                                                color: theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: 13
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: root.displayStatus(relayCard.modelData.status)
                                                color: relayCard.modelData.status === "Attention"
                                                       ? theme.gold : theme.inkDim
                                                font.family: theme.ui
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                }
            }

            Rectangle {
                objectName: "trackerAttentionRegion"
                Layout.fillWidth: true
                Layout.bottomMargin: 22
                implicitHeight: 62
                visible: root.attentionProviderCount > 0 || root.unresolvedCount > 0
                radius: 16
                color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.05)
                border.width: 1
                border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.35)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 12
                    Text {
                        text: "!"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 19
                        font.weight: Font.Bold
                    }
                    Text {
                        text: root.unresolvedCount > 0
                              ? root.unresolvedCount + " tracker items need review"
                              : root.attentionProviderCount + " trackers need attention"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Native History is unchanged."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
            }

            ColumnLayout {
                id: connectedSection
                objectName: "trackerConnectedSection"
                Layout.fillWidth: true
                Layout.bottomMargin: 34
                spacing: 16
                visible: root.connectedTrackers.length > 0

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Your trackers"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 27
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "This profile"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }

                GridLayout {
                    id: connectedGrid
                    objectName: "trackerConnectedGrid"
                    Layout.fillWidth: true
                    columns: root.connectedColumns
                    columnSpacing: 14
                    rowSpacing: 14

                    Repeater {
                        model: root.connectedTrackers
                        delegate: Button {
                            id: trackerCard
                            required property var modelData
                            objectName: "trackerCard_" + modelData.providerKey
                            Layout.fillWidth: true
                            Layout.preferredHeight: 177
                            activeFocusOnTab: true
                            Accessible.name: (modelData.providerName || "Tracker") + ", "
                                             + root.displayStatus(modelData.status)
                            onClicked: modelData.status === "Attention"
                                       ? root.openRecoveryRoute(modelData.providerKey, trackerCard)
                                       : root.openDossier(modelData.providerKey, trackerCard)

                            background: Rectangle {
                                radius: 18
                                color: trackerCard.down || trackerCard.hovered
                                       ? Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.075)
                                       : Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.045)
                                border.width: trackerCard.activeFocus ? 1.5 : 1
                                border.color: trackerCard.modelData.status === "Attention"
                                              ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42)
                                              : (trackerCard.activeFocus ? theme.gold : theme.edge)
                                Behavior on color {
                                    enabled: root.motionEnabled
                                    ColorAnimation { duration: 140 }
                                }
                            }

                            contentItem: ColumnLayout {
                                spacing: 14
                                RowLayout {
                                    spacing: 12
                                    Rectangle {
                                        Layout.preferredWidth: 44
                                        Layout.preferredHeight: 44
                                        radius: 12
                                        color: theme.glassTint
                                        border.width: 1
                                        border.color: theme.edge
                                        Image {
                                            objectName: "trackerConnectedIcon_" + trackerCard.modelData.providerKey
                                            anchors.centerIn: parent
                                            source: root.providerIcon(trackerCard.modelData.providerKey)
                                            width: 28; height: 28
                                            fillMode: Image.PreserveAspectFit
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text {
                                            text: trackerCard.modelData.providerName || "Tracker"
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: trackerCard.modelData.accountLabel || "Connected to this profile"
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Text {
                                        text: root.displayStatus(trackerCard.modelData.status)
                                        color: trackerCard.modelData.status === "Attention" ? theme.gold : theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 4
                                    spacing: 18
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text {
                                            text: root.lastSyncLabel(trackerCard.modelData.lastSuccessfulSyncAtMs)
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 14
                                        }
                                        Text {
                                            text: "LAST SYNC"
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 9
                                            font.letterSpacing: 0.6
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text {
                                            text: String(Number(trackerCard.modelData.unresolvedCount || 0)
                                                         + Number(trackerCard.modelData.waitingCount || 0))
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 14
                                        }
                                        Text {
                                            text: "WAITING / REVIEW"
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 9
                                            font.letterSpacing: 0.6
                                        }
                                    }
                                    Text {
                                        text: "Open ›"
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                id: mainSyncSection
                objectName: "trackerMainSyncSection"
                Layout.fillWidth: true
                Layout.bottomMargin: 16
                spacing: 12

                Text {
                    objectName: "trackerExternalServicesTitle"
                    text: "External services"
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 27
                }
                Text {
                    objectName: "trackerExternalServicesDescription"
                    text: "Manage connections to external libraries."
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 11
                }

                Rectangle {
                    objectName: "trackerStremioCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    radius: 16
                    color: Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.96)
                    border.width: 1
                    border.color: theme.edge

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 24
                        spacing: 22

                        Image {
                            objectName: "trackerStremioIcon"
                            source: "../assets/icons/stremio-official.svg"
                            Layout.preferredWidth: 74
                            Layout.preferredHeight: 74
                            fillMode: Image.PreserveAspectFit
                        }
                        ColumnLayout {
                            spacing: 6
                            Text {
                                text: "Stremio"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 24
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: "Library, progress, and History"
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                            }
                            RowLayout {
                                visible: root.width >= 760
                                spacing: 6
                                Repeater {
                                    model: ["Library", "Progress", "History"]
                                    delegate: Rectangle {
                                        id: stremioTag
                                        required property string modelData
                                        objectName: "trackerStremioCapabilityTag_"
                                                     + modelData.toLowerCase()
                                        implicitWidth: tagLabel.implicitWidth + 14
                                        implicitHeight: 21
                                        radius: 5
                                        color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.075)
                                        border.width: 0
                                        Text {
                                            id: tagLabel
                                            objectName: "trackerStremioCapabilityLabel_"
                                                         + stremioTag.modelData.toLowerCase()
                                            anchors.centerIn: parent
                                            text: stremioTag.modelData
                                            color: theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        ColumnLayout {
                            spacing: 7
                            Rectangle {
                                objectName: "trackerStremioStatusBadge"
                                Layout.alignment: Qt.AlignRight
                                implicitWidth: stremioStatusText.implicitWidth + 24
                                implicitHeight: 28
                                radius: 10
                                color: theme.glassTint
                                border.width: 1
                                border.color: theme.edge
                                Text {
                                    id: stremioStatusText
                                    anchors.centerIn: parent
                                    text: root.stremioStatusLabel()
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                }
                            }
                            Button {
                                id: mainSyncButton
                                objectName: "trackerMainSyncButton"
                                Layout.preferredWidth: 142
                                Layout.preferredHeight: 40
                                activeFocusOnTab: true
                                Accessible.name: "Open Stremio settings"
                                onClicked: root.mainSyncRequested()
                                background: Rectangle {
                                    radius: 9
                                    color: mainSyncButton.down || mainSyncButton.hovered
                                           ? theme.glassHi : theme.glassTint
                                    border.width: mainSyncButton.activeFocus ? 2 : 1
                                    border.color: theme.gold
                                }
                                contentItem: Text {
                                    text: "Open settings ›"
                                    color: theme.gold
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                id: catalogueSection
                objectName: "trackerCatalogueSection"
                Layout.fillWidth: true
                Layout.bottomMargin: 120
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "trackerCatalogueTitle"
                        text: "Available trackers"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 27
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Capabilities are verified per provider"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }

                GridLayout {
                    id: catalogueGrid
                    objectName: "trackerCatalogueGrid"
                    Layout.fillWidth: true
                    columns: root.catalogueColumns
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        model: root.catalogue
                        delegate: Button {
                            id: catalogueCard
                            required property var modelData
                            objectName: "trackerCatalogue_" + modelData.providerKey
                            Layout.fillWidth: true
                            Layout.preferredHeight: 130
                            padding: 15
                            activeFocusOnTab: true
                            Accessible.name: (modelData.providerName || "Tracker") + ", "
                                             + (modelData.status || "Unavailable")
                            onClicked: root.openDossier(modelData.providerKey, catalogueCard)

                            background: Rectangle {
                                radius: 17
                                color: catalogueCard.down || catalogueCard.hovered
                                       ? Qt.rgba(12 / 255, 15 / 255, 24 / 255, 0.96)
                                       : Qt.rgba(6 / 255, 7 / 255, 11 / 255, 0.92)
                                border.width: catalogueCard.activeFocus ? 1.5 : 1
                                border.color: catalogueCard.activeFocus ? theme.gold : theme.edge
                            }

                            contentItem: ColumnLayout {
                                spacing: 9
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 13
                                    Image {
                                        objectName: "trackerProviderIcon_" + catalogueCard.modelData.providerKey
                                        source: root.providerIcon(catalogueCard.modelData.providerKey)
                                        Layout.preferredWidth: 54
                                        Layout.preferredHeight: 54
                                        Layout.alignment: Qt.AlignTop
                                        fillMode: Image.PreserveAspectFit
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 5
                                        Text {
                                            text: catalogueCard.modelData.providerName || "Tracker"
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: root.providerSubject(catalogueCard.modelData.providerKey)
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.alignment: Qt.AlignRight
                                    implicitWidth: catalogueStateText.implicitWidth + 22
                                    implicitHeight: 28
                                    radius: 7
                                    color: theme.glassTint
                                    border.width: 1
                                    border.color: theme.edge
                                    Text {
                                        id: catalogueStateText
                                        anchors.centerIn: parent
                                        text: catalogueCard.modelData.available
                                              ? (catalogueCard.modelData.connected ? "Connected"
                                                 : (catalogueCard.modelData.pendingWork === true
                                                    ? "Needs attention" : "Connect"))
                                              : "Unavailable in this build"
                                        color: catalogueCard.modelData.available
                                               ? (catalogueCard.modelData.pendingWork === true
                                                  ? theme.gold : theme.inkDim)
                                               : theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Right-side provider dossier. Opening it reads a safe native projection only;
    // no provider operation is started by visiting a card or its details.
    Item {
        id: dossierVeil
        objectName: "trackerDossierVeil"
        anchors.fill: parent
        visible: root.dossierOpen
        z: 20

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.48)
            MouseArea {
                anchors.fill: parent
                onClicked: root.closeDossier()
            }
        }

        FocusScope {
            id: dossierFocus
            objectName: "trackerProviderDossier"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 18
            width: Math.min(610, root.width - 36)
            focus: visible
            activeFocusOnTab: true
            Accessible.role: Accessible.Dialog
            Accessible.name: root.selectedDossier.providerName || "Tracker details"
            KeyboardSpatialNavigator { id: dossierSpatialNav; root: dossierFocus }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (!event.accepted)
                    dossierSpatialNav.handle(event)
            }
            Keys.onReleased: (event) => dossierSpatialNav.handleRelease(event)

            Rectangle {
                anchors.fill: parent
                radius: 22
                color: theme.biblioWashBottom
                border.width: 1
                border.color: theme.edge
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 26
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                        radius: 15
                        color: theme.glassTint
                        border.width: 1
                        border.color: theme.edge
                        Image {
                            anchors.centerIn: parent
                            source: root.selectedProviderKey === "global"
                                    ? "../assets/icons/preferences.svg"
                                    : root.providerIcon(root.selectedProviderKey)
                            width: 32; height: 32
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: root.selectedProviderKey === "global" ? "ALL TRACKERS"
                                  : (root.selectedDossier.connected ? "CONNECTED TRACKER"
                                     : (root.selectedDossier.pendingWork === true
                                        ? "PAUSED TRACKER WORK" : "AVAILABLE TRACKER"))
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 9
                            font.letterSpacing: 1.4
                            font.weight: Font.Bold
                        }
                        Text {
                            id: dossierTitle
                            objectName: "trackerDossierTitle"
                            text: root.selectedDossier.providerName || "Tracker"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 24
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Button {
                        id: dossierCloseButton
                        objectName: "trackerDossierClose"
                        activeFocusOnTab: true
                        Accessible.name: "Close tracker details"
                        onClicked: root.closeDossier()
                        contentItem: Text {
                            text: "×"
                            color: dossierCloseButton.activeFocus ? theme.gold : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 22
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 11
                            color: dossierCloseButton.down ? theme.glassHi : "transparent"
                            border.width: dossierCloseButton.activeFocus ? 1 : 0
                            border.color: theme.gold
                        }
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.08)
                }

                Flickable {
                    id: dossierScroll
                    objectName: "trackerDossierScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: dossierColumn.implicitHeight + 52
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: dossierColumn
                        x: 28
                        y: 24
                        width: dossierScroll.width - 56
                        spacing: 25

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: healthColumn.implicitHeight + 34
                            radius: 15
                            color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.045)
                            border.width: 1
                            border.color: theme.edge

                            ColumnLayout {
                                id: healthColumn
                                anchors.fill: parent
                                anchors.margins: 17
                                spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        id: dossierStatus
                                        objectName: "trackerDossierStatus"
                                        text: root.displayStatus(root.selectedDossier.status)
                                        color: root.selectedDossier.status === "Attention" ? theme.gold : theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: root.selectedDossier.connected ? "THIS PROFILE" : "CAPABILITY GATE"
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 9
                                        font.letterSpacing: 0.8
                                    }
                                }
                                Text {
                                    text: root.selectedProviderKey === "global"
                                          ? "Trackers are optional. Colosseum saves local changes first."
                                          : (!root.selectedDossier.available
                                             ? "This provider is unavailable in this build. Colosseum's native progress, History, and activity remain available."
                                             : (root.selectedDossier.pendingWork === true
                                                ? "Tracker work remains paused on this profile. Reconnect the same account to check uncertain delivery before any retry."
                                                : (root.selectedDossier.connected
                                                   ? "Provider status and actions follow the verified capability snapshot."
                                                   : "Connect only when this build has a verified provider authentication path.")))
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                Button {
                                    id: connectButton
                                    objectName: "trackerConnectButton"
                                    visible: root.selectedProviderKey !== "global"
                                             && root.selectedDossier.connected !== true
                                    enabled: root.selectedDossier.connectEnabled === true
                                             && root.selectedDossier.available === true
                                    text: root.selectedDossier.pendingWork === true
                                          ? "Reconnect to recover pending work"
                                          : (root.selectedDossier.available ? "Connect" : "Not available")
                                    Accessible.name: root.selectedDossier.available
                                                     ? (root.selectedDossier.pendingWork === true
                                                        ? "Reconnect the same "
                                                          + (root.selectedDossier.providerName || "tracker")
                                                          + " account to recover pending work"
                                                        : "Connect "
                                                          + (root.selectedDossier.providerName || "tracker"))
                                                     : "Provider unavailable in this build"
                                    Layout.alignment: Qt.AlignLeft
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                text: "Account and profile"
                                visible: root.selectedDossier.connected === true
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 20
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: accountRow.implicitHeight + 18
                                visible: root.selectedDossier.connected === true
                                color: "transparent"
                                border.width: 0
                                RowLayout {
                                    id: accountRow
                                    anchors.fill: parent
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text {
                                            text: root.selectedDossier.accountLabel || "Connected account"
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                        }
                                        Text {
                                            text: "Only this Colosseum profile can use this connection"
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                            Button {
                                id: disconnectButton
                                objectName: "trackerDisconnectButton"
                                visible: root.selectedDossier.connected === true
                                         || root.selectedDossier.cleanupEnabled === true
                                enabled: root.selectedDossier.connected === true
                                         ? root.selectedDossier.disconnectEnabled === true
                                         : root.selectedDossier.cleanupEnabled === true
                                text: root.disconnectCleanupMode()
                                      ? "Finish removing known-unsent updates"
                                      : "Disconnect from this profile"
                                Accessible.name: root.disconnectCleanupMode()
                                                 ? "Finish removing known-unsent updates for "
                                                   + (root.selectedDossier.providerName || "tracker")
                                                 : "Disconnect "
                                                   + (root.selectedDossier.providerName || "tracker")
                                                   + " from this Colosseum profile"
                                Layout.alignment: Qt.AlignLeft
                                onClicked: root.openDisconnectReview()
                            }
                            Button {
                                id: removeImportedButton
                                objectName: "trackerRemoveImportedData"
                                visible: root.selectedDossier.removeImportedEnabled === true
                                         && (Number(root.selectedDossier.importedHistoryCount || 0) > 0
                                             || Number(root.selectedDossier.importedProgressCount || 0) > 0)
                                enabled: visible
                                text: "Remove imported data"
                                Accessible.name: "Review removal of "
                                                 + Number(root.selectedDossier.importedHistoryCount || 0)
                                                 + " tracker-imported History items and "
                                                 + Number(root.selectedDossier.importedProgressCount || 0)
                                                 + " tracker-imported Progress items from "
                                                 + (root.selectedDossier.providerName || "tracker")
                                Layout.alignment: Qt.AlignLeft
                                onClicked: root.openImportedDataReview()
                            }
                            Text {
                                objectName: "trackerDisconnectInFlightNotice"
                                visible: root.selectedDossier.disconnectInFlight === true
                                text: "A tracker update is still sending. Wait for its result before disconnecting."
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Text {
                                objectName: "trackerDisconnectNotice"
                                visible: root.actionNotice.length > 0
                                text: root.actionNotice
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        ColumnLayout {
                            objectName: "trackerCapabilitySection"
                            Layout.fillWidth: true
                            spacing: 12
                            Text {
                                text: "Capabilities"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 20
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 7
                                Repeater {
                                    model: root.capabilitiesForDisplay(root.selectedDossier.capabilities)
                                    delegate: Rectangle {
                                        required property string modelData
                                        width: capabilityText.implicitWidth + 20
                                        height: 30
                                        radius: 15
                                        color: "transparent"
                                        border.width: 1
                                        border.color: theme.edge
                                        Text {
                                            id: capabilityText
                                            anchors.centerIn: parent
                                            text: modelData
                                            color: theme.inkDim
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                            Text {
                                objectName: "trackerNoVerifiedCapabilities"
                                visible: root.capabilitiesForDisplay(root.selectedDossier.capabilities).length === 0
                                         && root.selectedProviderKey !== "global"
                                text: "No capabilities are verified in this build."
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                            }
                        }

                        ColumnLayout {
                            objectName: "trackerImportReviewSection"
                            Layout.fillWidth: true
                            spacing: 10
                            visible: root.importReviewsForSelectedProvider().length > 0
                                     && root.selectedDossier.connected === true
                            Text {
                                text: "Import review"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 20
                            }
                            Repeater {
                                model: root.importReviewsForSelectedProvider()
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: importBatchRow.implicitHeight + 26
                                    radius: 14
                                    color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.035)
                                    border.width: 1
                                    border.color: theme.edge

                                    RowLayout {
                                        id: importBatchRow
                                        anchors.fill: parent
                                        anchors.margins: 13
                                        spacing: 14
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            Text {
                                                text: modelData.initialImport ? "Initial import" : "Progress update"
                                                color: theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                            }
                                            Text {
                                                objectName: "trackerImportBatchSummary"
                                                text: root.importBatchSummary(modelData)
                                                color: theme.inkDim
                                                font.family: theme.ui
                                                font.pixelSize: 10
                                            }
                                        }
                                        Button {
                                            id: importBatchReviewButton
                                            objectName: "trackerImportReviewButton_" + modelData.batchId
                                            activeFocusOnTab: true
                                            text: modelData.confirmed ? "View review" : "Review import"
                                            Accessible.name: text + " from " + (modelData.providerName || "tracker")
                                            onClicked: root.openImportReview(modelData.batchId,
                                                                              importBatchReviewButton)
                                            contentItem: Text {
                                                text: importBatchReviewButton.text
                                                color: importBatchReviewButton.activeFocus ? theme.gold : theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle {
                                                radius: 10
                                                color: importBatchReviewButton.down ? theme.glassHi : theme.glassTint
                                                border.width: 1
                                                border.color: importBatchReviewButton.activeFocus ? theme.gold : theme.edge
                                            }
                                            padding: 12
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            objectName: "trackerDeliveryQueueSection"
                            Layout.fillWidth: true
                            spacing: 10
                            visible: root.selectedProviderKey !== "global"
                                     && (root.selectedDossier.deliveryRows || []).length > 0
                            Text {
                                text: "Delivery status"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 20
                            }
                            Text {
                                objectName: "trackerDisconnectedDeliveryNotice"
                                visible: root.selectedDossier.connected !== true
                                text: root.hasUncertainDelivery()
                                      ? "Uncertain delivery stays paused until this same tracker account reconnects. Colosseum checks before retrying."
                                      : "Pending tracker work stays paused until this account reconnects."
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Repeater {
                                id: deliveryStatusRows
                                model: root.selectedDossier.deliveryRows || []
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    objectName: "trackerDeliveryStatusRow_" + index
                                    Layout.fillWidth: true
                                    implicitHeight: deliveryRowContent.implicitHeight + 24
                                    radius: 14
                                    activeFocusOnTab: root.recoveryRouteActive
                                                      && root.recoveryRouteProviderKey
                                                         === root.selectedProviderKey
                                                      && root.recoveryRouteFocusIndex === index
                                    color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.035)
                                    border.width: 1
                                    border.color: modelData.state === "checking_delivery"
                                                  || modelData.state === "needs_attention"
                                                  || modelData.state === "failed"
                                                  ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42)
                                                  : theme.edge
                                    ColumnLayout {
                                        id: deliveryRowContent
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 5
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: modelData.title || "Playback update"
                                                color: theme.ink
                                                font.family: theme.ui
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                objectName: "trackerDeliveryState"
                                                text: root.deliveryStateLabel(modelData.state)
                                                color: modelData.state === "checking_delivery"
                                                       || modelData.state === "needs_attention"
                                                       || modelData.state === "failed"
                                                       ? theme.gold : theme.inkDim
                                                font.family: theme.ui
                                                font.pixelSize: 10
                                            }
                                        }
                                        Text {
                                            text: root.deliveryReasonDetail(modelData.reason)
                                            color: theme.inkDimmer
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            objectName: "trackerAutomaticSyncSummary"
                            Layout.fillWidth: true
                            spacing: 0
                            visible: root.selectedProviderKey !== "global"
                                     && root.selectedDossier.connected === true
                            Text {
                                text: "Automatic sync"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 20
                                Layout.bottomMargin: 8
                            }
                            TrackerSettingToggle {
                                objectName: "trackerPullAutomaticallyToggle"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Pull changes automatically"
                                checked: root.selectedDossier.pullAutomatically === true
                                enabled: root.selectedDossier.pullSettingEnabled === true
                                onToggled: root.saveProviderSetting("pull", checked)
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.08) }
                            TrackerSettingToggle {
                                objectName: "trackerSendProgressToggle"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Send Colosseum progress"
                                checked: root.selectedDossier.sendProgressEnabled === true
                                enabled: root.selectedDossier.sendSettingEnabled === true
                                onToggled: root.saveProviderSetting("send", checked)
                            }
                            Button {
                                id: exportReviewButton
                                objectName: "trackerExportReviewButton"
                                Layout.alignment: Qt.AlignLeft
                                Layout.topMargin: 6
                                Layout.bottomMargin: 9
                                visible: root.selectedDossier.exportReviewed !== true
                                         && ((root.selectedDossier.capabilities || []).includes("write_progress")
                                             || (root.selectedDossier.capabilities || []).includes("write_completion"))
                                enabled: root.selectedDossier.exportReviewEnabled === true
                                activeFocusOnTab: true
                                text: "Review Colosseum updates before sending"
                                Accessible.name: text + " to " + (root.selectedDossier.providerName || "tracker")
                                onClicked: root.openExportReview()
                                contentItem: Text {
                                    text: exportReviewButton.text
                                    color: exportReviewButton.enabled ? theme.gold : theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: exportReviewButton.down ? theme.glassHi : theme.glassTint
                                    border.width: 1
                                    border.color: exportReviewButton.activeFocus ? theme.gold : theme.edge
                                }
                                padding: 10
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.08) }
                            TrackerSettingToggle {
                                objectName: "trackerLivePlaybackToggle"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Live playback tracking"
                                checked: root.selectedDossier.livePlaybackTrackingEnabled === true
                                enabled: root.selectedDossier.livePlaybackSettingEnabled === true
                                onToggled: root.saveProviderSetting("live", checked)
                            }
                        }

                        ColumnLayout {
                            objectName: "trackerGlobalPreferencesSummary"
                            Layout.fillWidth: true
                            spacing: 0
                            visible: root.selectedProviderKey === "global"
                            Text {
                                text: "Current preferences"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 20
                                Layout.bottomMargin: 8
                            }
                            TrackerSettingToggle {
                                objectName: "trackerSetting_trackerSyncEnabled"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Tracker sync"
                                checked: root.selectedDossier.trackerSyncEnabled === true
                                enabled: root.selectedDossier.editable === true
                                onToggled: root.saveGlobalSetting("trackerSyncEnabled", checked)
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.08) }
                            TrackerSettingToggle {
                                objectName: "trackerSetting_checkOnLaunch"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Check connected trackers on launch"
                                checked: root.selectedDossier.checkOnLaunch === true
                                enabled: root.selectedDossier.editable === true
                                onToggled: root.saveGlobalSetting("checkOnLaunch", checked)
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.08) }
                            TrackerSettingToggle {
                                objectName: "trackerSetting_backgroundDelivery"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Deliver in the background"
                                checked: root.selectedDossier.backgroundDelivery === true
                                enabled: root.selectedDossier.editable === true
                                onToggled: root.saveGlobalSetting("backgroundDelivery", checked)
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.08) }
                            TrackerSettingToggle {
                                objectName: "trackerSetting_completionMessages"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                settingLabel: "Show completion messages"
                                checked: root.selectedDossier.completionMessages === true
                                enabled: root.selectedDossier.editable === true
                                onToggled: root.saveGlobalSetting("completionMessages", checked)
                            }
                            Text {
                                objectName: "trackerSettingNotice"
                                visible: root.actionNotice.length > 0
                                text: root.actionNotice
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: root.selectedDossier.editable !== true
                                text: "Settings are unavailable for this profile."
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 10
                                Layout.topMargin: 8
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: exportReviewVeil
        objectName: "trackerExportReviewVeil"
        anchors.fill: parent
        visible: root.exportReviewOpen
        z: 35

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.68)
            MouseArea { anchors.fill: parent }
        }

        FocusScope {
            id: exportReviewDialog
            objectName: "trackerExportReviewDialog"
            anchors.fill: parent
            focus: visible
            activeFocusOnTab: true
            Accessible.role: Accessible.Dialog
            Accessible.name: "Review Colosseum updates before sending"
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.closeExportReview()
                    event.accepted = true
                }
            }

            FocusScope {
                id: exportReviewContent
                anchors.centerIn: parent
                width: Math.min(650, root.width - 48)
                height: Math.min(650, root.height - 48)
                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: theme.biblioWashBottom
                    border.width: 1
                    border.color: theme.edge
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 25
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            objectName: "trackerExportReviewTitle"
                            text: "Send to " + (root.selectedExportReview.providerName || "tracker")
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 24
                            Layout.fillWidth: true
                        }
                        Button {
                            id: exportReviewCloseButton
                            objectName: "trackerExportReviewClose"
                            text: "×"
                            activeFocusOnTab: true
                            Accessible.name: "Close send review"
                            KeyNavigation.tab: exportReviewList
                            onClicked: root.closeExportReview()
                            contentItem: Text {
                                text: exportReviewCloseButton.text
                                color: exportReviewCloseButton.activeFocus ? theme.gold : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 22
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 11
                                color: exportReviewCloseButton.down ? theme.glassHi : "transparent"
                                border.width: exportReviewCloseButton.activeFocus ? 1 : 0
                                border.color: theme.gold
                            }
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                        }
                    }
                    Text {
                        text: "Choose the Colosseum updates you want to send. This review is separate from importing tracker data."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerExportReviewEmptyNotice"
                        visible: root.exportReviewOpen
                                 && Number(root.selectedExportReview.eligibleCount || 0) === 0
                        text: (root.selectedExportReview.items || []).length === 0
                              ? "There are no local updates to send. Close this review."
                              : "No local updates can be sent from this review. The reasons are shown below; close this review."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ListView {
                        id: exportReviewList
                        objectName: "trackerExportReviewItems"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        activeFocusOnTab: true
                        clip: true
                        spacing: 6
                        model: root.selectedExportReview.items || []
                        boundsBehavior: Flickable.StopAtBounds
                        KeyNavigation.tab: exportReviewConfirmButton
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return) {
                                var row = root.selectedExportReview.items[currentIndex]
                                if (row) root.toggleExportItem(row.itemId, row.eligible)
                                event.accepted = true
                            }
                        }
                        ScrollBar.vertical: HouseScrollBar { flick: exportReviewList }
                        delegate: CheckBox {
                            required property var modelData
                            objectName: "trackerExportReviewItem_" + String(modelData.itemId || "")
                            width: ListView.view.width
                            height: 82
                            leftPadding: 45
                            rightPadding: 12
                            enabled: modelData.eligible === true && root.exportReviewIsCurrent()
                            checked: root.selectedExportItemIds.indexOf(modelData.itemId) >= 0
                            text: String(modelData.title || "Colosseum item") + " · "
                                  + String(modelData.kind || "Update")
                                  + (modelData.kind === "Progress" ? " " + Number(modelData.progress || 0) : "")
                                  + "\n" + (modelData.willChangeRemote === true
                                      ? "Tracker now: " + String(modelData.remoteBefore || "Existing tracker state")
                                        + " → after send: " + String(modelData.remoteAfter || "Local update")
                                        + " · " : "") + String(modelData.reason || "")
                            Accessible.name: text
                            onClicked: root.toggleExportItem(modelData.itemId, modelData.eligible)
                            indicator: Rectangle {
                                x: 12
                                y: (parent.height - height) / 2
                                width: 20
                                height: 20
                                radius: 5
                                color: parent.checked ? theme.gold : theme.glassTint
                                border.width: 1
                                border.color: parent.activeFocus ? theme.gold : theme.edge
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    visible: parent.parent.checked
                                    color: theme.biblioWashBottom
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.Bold
                                }
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? theme.ink : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.WordWrap
                            }
                            background: Rectangle {
                                radius: 11
                                color: parent.checked ? theme.glassHi : theme.glassTint
                                border.width: 1
                                border.color: parent.activeFocus ? theme.gold : theme.edge
                            }
                        }
                    }
                    Text {
                        objectName: "trackerExportReviewNotice"
                        visible: root.exportReviewNotice.length > 0
                        text: root.exportReviewNotice
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: root.selectedExportItemIds.length + " selected"
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        Button {
                            id: exportReviewConfirmButton
                            objectName: "trackerExportReviewConfirm"
                            text: "Send " + root.selectedExportItemIds.length + " to "
                                  + (root.selectedExportReview.providerName || "tracker")
                            enabled: root.exportReviewIsCurrent()
                                     && root.selectedExportItemIds.length > 0
                            activeFocusOnTab: true
                            Accessible.name: text
                            KeyNavigation.tab: exportReviewCloseButton
                            onClicked: root.confirmExportReview()
                            contentItem: Text {
                                text: exportReviewConfirmButton.text
                                color: exportReviewConfirmButton.enabled ? theme.gold : theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 11
                                color: exportReviewConfirmButton.enabled
                                       ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
                                       : theme.glassTint
                                border.width: 1
                                border.color: exportReviewConfirmButton.activeFocus ? theme.gold
                                              : exportReviewConfirmButton.enabled
                                                ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42)
                                                : theme.edge
                            }
                            padding: 13
                        }
                    }
                }
            }
        }
    }

    Item {
        id: importReviewVeil
        objectName: "trackerImportReviewVeil"
        anchors.fill: parent
        visible: root.importReviewOpen
        z: 30

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.68)
            MouseArea { anchors.fill: parent }
        }

        FocusScope {
            id: importReviewDialog
            objectName: "trackerImportReviewDialog"
            anchors.fill: parent
            focus: visible
            activeFocusOnTab: true

            FocusScope {
                    id: importReviewContent
                    anchors.centerIn: parent
                    width: Math.min(860, root.width - 48)
                    height: Math.min(760, root.height - 48)
                    Accessible.role: Accessible.Dialog
                    Accessible.name: "Tracker import review"
                    Keys.priority: Keys.BeforeItem
                    KeyboardSpatialNavigator { id: importReviewSpatialNav; root: importReviewContent }
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Escape) {
                            root.closeImportReview()
                            event.accepted = true
                            return
                        }
                        if (!event.accepted)
                            importReviewSpatialNav.handle(event)
                    }
                    Keys.onReleased: (event) => importReviewSpatialNav.handleRelease(event)

                    Rectangle {
                        anchors.fill: parent
                        radius: 22
                        color: theme.biblioWashBottom
                        border.width: 1
                        border.color: theme.edge
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 25
                        spacing: 14

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                objectName: "trackerImportReviewTitle"
                                text: (root.selectedImportSnapshot.initialImport ? "Initial import · " : "Progress update · ")
                                      + (root.selectedImportSnapshot.providerName || "Tracker")
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 25
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                Accessible.role: Accessible.Heading
                            }
                            Button {
                                id: importReviewCloseButton
                                objectName: "trackerImportReviewClose"
                                activeFocusOnTab: true
                                Accessible.name: "Close import review"
                                text: "×"
                                KeyNavigation.backtab: cancelImportReviewButton
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: (event) => root.handleImportReviewKeys(event)
                                onClicked: root.closeImportReview()
                                contentItem: Text {
                                    text: importReviewCloseButton.text
                                    color: importReviewCloseButton.activeFocus ? theme.gold : theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 22
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 11
                                    color: importReviewCloseButton.down ? theme.glassHi : "transparent"
                                    border.width: importReviewCloseButton.activeFocus ? 1 : 0
                                    border.color: theme.gold
                                }
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                            }
                        }

                        Text {
                            objectName: "trackerImportHistoryProtection"
                            text: "Review each item before importing. Colosseum’s native History and witnessed activity stay protected; tracker progress never creates a watched event by itself."
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        ColumnLayout {
                            objectName: "trackerImportBulkSelection"
                            Layout.fillWidth: true
                            spacing: 8
                            visible: root.selectedBulkImportItemIds.length > 0
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    objectName: "trackerImportBulkSelectionCount"
                                    text: root.selectedBulkImportItemIds.length + " items selected"
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                }
                                Button {
                                    objectName: "trackerImportBulkReviewButton"
                                    activeFocusOnTab: true
                                    text: "Review shared choices"
                                    enabled: root.importReviewIsCurrent()
                                             && root.selectedBulkImportItemIds.length >= 2
                                             && root.bulkImportCommonChoices().length > 0
                                    Accessible.name: text + " for "
                                                     + root.selectedBulkImportItemIds.length
                                                     + " selected import items"
                                    onClicked: root.openBulkImportReview()
                                }
                                Button {
                                    objectName: "trackerImportBulkClearButton"
                                    activeFocusOnTab: true
                                    text: "Clear selection"
                                    Accessible.name: "Clear selected tracker import items"
                                    onClicked: root.selectedBulkImportItemIds = []
                                }
                            }
                            Text {
                                objectName: "trackerImportBulkExceptionNotice"
                                visible: root.selectedBulkImportItemIds.length >= 2
                                         && root.bulkImportCommonChoices().length === 0
                                text: "These selections do not share a safe choice. Review each item separately; protected and unmatched exceptions stay visible."
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            objectName: "trackerImportPageIncomplete"
                            visible: root.selectedImportSnapshot.accepted === true
                                     && root.selectedImportSnapshot.pageComplete !== true
                            text: "More tracker pages are still being read. You can inspect this preview, but confirmation stays unavailable until it is complete."
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        ListView {
                            id: importReviewList
                            objectName: "trackerImportReviewItems"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 9
                            model: root.selectedImportSnapshot.items || []
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: HouseScrollBar { flick: importReviewList }
                            delegate: Rectangle {
                                id: importReviewItemCard
                                required property var modelData
                                property Item firstDecisionButton: null
                                width: ListView.view.width
                                height: importReviewItemContent.implicitHeight + 26
                                radius: 14
                                color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.045)
                                border.width: 1
                                border.color: importReviewItemCard.modelData.nativeHistoryProtected
                                              ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.46)
                                              : theme.edge

                                ColumnLayout {
                                    id: importReviewItemContent
                                    anchors.fill: parent
                                    anchors.margins: 13
                                    spacing: 7
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            objectName: "trackerImportBulkSelect_"
                                                         + importReviewItemCard.modelData.reviewItemId
                                            visible: (importReviewItemCard.modelData.allowedChoices || []).length > 0
                                            enabled: root.importReviewIsCurrent()
                                            checkable: true
                                            checked: root.bulkImportSelectionContains(
                                                         importReviewItemCard.modelData.reviewItemId)
                                            text: checked ? "Selected" : "Select"
                                            Accessible.name: (checked ? "Remove " : "Select ")
                                                             + (importReviewItemCard.modelData.title || "untitled entry")
                                                             + " for a group decision"
                                            activeFocusOnTab: true
                                            onClicked: root.toggleBulkImportItem(
                                                           importReviewItemCard.modelData.reviewItemId)
                                            contentItem: Text {
                                                text: parent.checked ? "Selected" : "Select"
                                                color: parent.activeFocus ? theme.gold : theme.inkDim
                                                font.family: theme.ui
                                                font.pixelSize: 9
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle {
                                                radius: 8
                                                color: parent.checked ? theme.glassHi : theme.glassTint
                                                border.width: 1
                                                border.color: parent.activeFocus ? theme.gold : theme.edge
                                            }
                                            padding: 8
                                        }
                                        Text {
                                            text: importReviewItemCard.modelData.title || "Untitled entry"
                                            color: theme.ink
                                            font.family: theme.ui
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: root.importClassificationLabel(importReviewItemCard.modelData.classification)
                                            color: theme.gold
                                            font.family: theme.ui
                                            font.pixelSize: 10
                                        }
                                    }
                                    Text {
                                        text: root.importProgressSummary(importReviewItemCard.modelData)
                                        color: theme.inkDim
                                        font.family: theme.ui
                                        font.pixelSize: 11
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                    Text {
                                        visible: importReviewItemCard.modelData.nativeHistoryProtected === true
                                        text: "Native History protects this title. Keep Colosseum progress is the only available choice."
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                    Text {
                                        visible: importReviewItemCard.modelData.classification === "needs_matching"
                                                 && !(importReviewItemCard.modelData.allowedChoices || []).includes("find_match")
                                        text: "Find match is unavailable right now. You can leave this entry unmatched."
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Repeater {
                                            model: importReviewItemCard.modelData.allowedChoices || []
                                            delegate: Button {
                                                id: importChoiceButton
                                                required property string modelData
                                                required property int index
                                                objectName: "trackerImportChoice_"
                                                             + importReviewItemCard.modelData.reviewItemId
                                                             + "_" + modelData
                                                text: root.importChoiceLabel(modelData)
                                                enabled: root.importReviewIsCurrent()
                                                Accessible.name: text + " for "
                                                                 + (importReviewItemCard.modelData.title || "untitled entry")
                                                activeFocusOnTab: true
                                                Component.onCompleted: {
                                                    if (index === 0)
                                                        importReviewItemCard.firstDecisionButton = importChoiceButton
                                                }
                                                Component.onDestruction: {
                                                    if (importReviewItemCard.firstDecisionButton === importChoiceButton)
                                                        importReviewItemCard.firstDecisionButton = null
                                                }
                                                Keys.priority: Keys.BeforeItem
                                                Keys.onPressed: (event) => root.handleImportReviewKeys(event)
                                                onClicked: root.resolveImportChoice(importReviewItemCard.modelData,
                                                                                    modelData)
                                                contentItem: Text {
                                                    text: importChoiceButton.text
                                                    color: importChoiceButton.activeFocus ? theme.gold : theme.ink
                                                    font.family: theme.ui
                                                    font.pixelSize: 10
                                                    font.weight: Font.DemiBold
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                background: Rectangle {
                                                    radius: 9
                                                    color: importChoiceButton.down ? theme.glassHi : theme.glassTint
                                                    border.width: 1
                                                    border.color: importChoiceButton.activeFocus ? theme.gold : theme.edge
                                                }
                                                padding: 10
                                            }
                                        }
                                    }
                                    Text {
                                        objectName: "trackerImportDecisionStatus_"
                                                     + importReviewItemCard.modelData.reviewItemId
                                        visible: importReviewItemCard.modelData.allowedChoices
                                                 && importReviewItemCard.modelData.allowedChoices.length === 0
                                                 && importReviewItemCard.modelData.state !== "review_required"
                                        text: importReviewItemCard.modelData.state === "waiting_to_apply"
                                              ? "Approved · waiting for Colosseum to apply progress"
                                              : importReviewItemCard.modelData.state === "unresolved"
                                                ? importReviewItemCard.modelData.classification === "needs_matching"
                                                  ? "Left unmatched · no Colosseum progress was changed"
                                                  : "Left for later · Colosseum progress was not changed"
                                                : importReviewItemCard.modelData.state === "applied"
                                                  ? "Applied to progress · History remains native"
                                                  : importReviewItemCard.modelData.state === "kept_colosseum"
                                                    ? "Kept Colosseum progress"
                                                : importReviewItemCard.modelData.state === "no_change"
                                                      ? (importReviewItemCard.modelData.classification === "unsupported"
                                                         ? "Title match saved · no verified Progress target; Progress and History stayed unchanged"
                                                         : "No change needed")
                                                      : "Waiting for review"
                                        color: theme.inkDimmer
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        Text {
                            objectName: "trackerImportReviewNotice"
                            visible: root.importReviewNotice.length > 0
                            text: root.importReviewNotice
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                id: confirmImportButton
                                objectName: "trackerConfirmImportButton"
                                activeFocusOnTab: true
                                enabled: root.importReviewCanConfirm()
                                text: root.selectedImportSnapshot.confirmed === true
                                      ? "Review confirmed" : "Confirm import review"
                                Accessible.name: text
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: (event) => root.handleImportReviewKeys(event)
                                onClicked: root.confirmImportReview()
                                contentItem: Text {
                                    text: confirmImportButton.text
                                    color: confirmImportButton.enabled ? theme.gold : theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 11
                                    color: confirmImportButton.enabled
                                           ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
                                           : theme.glassTint
                                    border.width: 1
                                    border.color: confirmImportButton.enabled
                                                ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.42)
                                                : theme.edge
                                }
                                padding: 13
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                id: cancelImportReviewButton
                                objectName: "trackerCancelImportReviewButton"
                                activeFocusOnTab: true
                                text: "Close"
                                Accessible.name: "Close import review"
                                KeyNavigation.tab: importReviewCloseButton
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: (event) => root.handleImportReviewKeys(event)
                                onClicked: root.closeImportReview()
                            }
                        }
                    }

                }
        }
    }

    Item {
        id: titleMatchVeil
        objectName: "trackerTitleMatchVeil"
        anchors.fill: parent
        visible: root.titleMatchOpen
        z: 35

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.7)
            MouseArea { anchors.fill: parent }
        }

        FocusScope {
            id: titleMatchDialog
            objectName: "trackerTitleMatchDialog"
            anchors.fill: parent
            focus: visible
            activeFocusOnTab: true
            Accessible.role: Accessible.Dialog
            Accessible.name: "Find a Colosseum Progress title"
            Keys.priority: Keys.BeforeItem
            KeyboardSpatialNavigator { id: titleMatchSpatialNav; root: titleMatchContent }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.closeTitleMatchFinder()
                    event.accepted = true
                    return
                }
                if (!event.accepted)
                    titleMatchSpatialNav.handle(event)
            }
            Keys.onReleased: (event) => titleMatchSpatialNav.handleRelease(event)

            FocusScope {
                id: titleMatchContent
                anchors.centerIn: parent
                width: Math.min(620, root.width - 48)
                height: Math.min(570, root.height - 48)
                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: theme.biblioWashBottom
                    border.width: 1
                    border.color: theme.edge
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            objectName: "trackerTitleMatchTitle"
                            text: "Find a Colosseum title"
                            color: theme.ink
                            font.family: theme.display
                            font.pixelSize: 24
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Heading
                        }
                        Button {
                            id: titleMatchCloseButton
                            objectName: "trackerTitleMatchClose"
                            activeFocusOnTab: true
                            text: "×"
                            Accessible.name: "Close title search"
                            onClicked: root.closeTitleMatchFinder()
                            contentItem: Text {
                                text: titleMatchCloseButton.text
                                color: titleMatchCloseButton.activeFocus ? theme.gold : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 22
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 11
                                color: titleMatchCloseButton.down ? theme.glassHi : "transparent"
                                border.width: titleMatchCloseButton.activeFocus ? 1 : 0
                                border.color: theme.gold
                            }
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                        }
                    }

                    Text {
                        objectName: "trackerTitleMatchExplanation"
                        text: "Tracker title: “" + root.titleMatchRemoteTitle + "”"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "Choose a named title already in Colosseum Progress. A title match alone cannot apply tracker Progress or change History without a verified exact target."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    TextField {
                        id: titleMatchSearchField
                        objectName: "trackerTitleMatchSearch"
                        activeFocusOnTab: true
                        Accessible.name: "Search Colosseum Progress titles"
                        placeholderText: "Search Progress titles"
                        text: root.titleMatchSearchText
                        onTextEdited: {
                            root.titleMatchSearchText = text
                            root.refreshTitleMatchCandidates()
                        }
                        color: theme.ink
                        selectionColor: theme.gold
                        selectedTextColor: "#141416"
                        font.family: theme.ui
                        font.pixelSize: 12
                        background: Rectangle {
                            radius: 10
                            color: theme.glassTint
                            border.width: titleMatchSearchField.activeFocus ? 1 : 0
                            border.color: theme.gold
                        }
                        KeyNavigation.tab: titleMatchList.count > 0
                                          ? (titleMatchList.itemAtIndex(0)
                                             || titleMatchCancelButton)
                                          : titleMatchCancelButton
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                    }

                    ListView {
                        id: titleMatchList
                        objectName: "trackerTitleMatchCandidates"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 7
                        model: root.titleMatchRows
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: HouseScrollBar { flick: titleMatchList }
                        delegate: Button {
                            required property var modelData
                            objectName: "trackerTitleMatchCandidate_"
                                         + String(modelData.candidateId || "")
                            width: ListView.view.width
                            height: 56
                            activeFocusOnTab: true
                            Accessible.name: String(modelData.displayName || "Untitled title")
                                             + ", " + String(modelData.mediaType || "Progress")
                                             + (String(modelData.displayContext || "").length
                                                ? ", " + String(modelData.displayContext) : "")
                                             + ", save title match"
                            onClicked: root.confirmTitleMatchCandidate(modelData.candidateId)
                            contentItem: Column {
                                spacing: 1
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                Text {
                                    width: parent.width
                                    text: String(modelData.displayName || "Untitled title")
                                    color: parent.parent.activeFocus ? theme.gold : theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    objectName: "trackerTitleMatchCandidateMetadata_"
                                                 + String(modelData.candidateId || "")
                                    width: parent.width
                                    text: String(modelData.mediaType || "Progress")
                                          + (String(modelData.displayContext || "").length
                                             ? " · " + String(modelData.displayContext) : "")
                                    color: theme.inkDim
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            background: Rectangle {
                                radius: 10
                                color: parent.down ? theme.glassHi : theme.glassTint
                                border.width: parent.activeFocus ? 1 : 0
                                border.color: theme.gold
                            }
                            padding: 12
                        }
                        Text {
                            anchors.centerIn: parent
                            objectName: "trackerTitleMatchEmptyState"
                            visible: titleMatchList.count === 0
                            text: root.titleMatchSearchText.trim().length
                                  ? "No selectable matching Colosseum Progress titles."
                                  : "No selectable named Colosseum Progress titles are available."
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                        }
                    }

                    Text {
                        objectName: "trackerTitleMatchNotice"
                        visible: root.titleMatchNotice.length > 0
                        text: root.titleMatchNotice
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            id: titleMatchCancelButton
                            objectName: "trackerTitleMatchCancel"
                            activeFocusOnTab: true
                            text: "Back to import review"
                            Accessible.name: text
                            KeyNavigation.tab: titleMatchCloseButton
                            onClicked: root.closeTitleMatchFinder()
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

            }
        }
    }

    Item {
        id: disconnectReviewVeil
        objectName: "trackerDisconnectVeil"
        anchors.fill: parent
        visible: root.disconnectReviewOpen
        z: 40

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.68)
            MouseArea { anchors.fill: parent }
        }

        FocusScope {
            id: disconnectReviewDialog
            objectName: "trackerDisconnectDialog"
            anchors.fill: parent
            focus: visible
            activeFocusOnTab: true
            Accessible.role: Accessible.Dialog
            Accessible.name: root.disconnectCleanupMode()
                              ? "Finish removing known-unsent tracker updates"
                              : "Disconnect tracker from this profile"
            KeyboardSpatialNavigator {
                id: disconnectReviewSpatialNav
                root: disconnectReviewContent
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.closeDisconnectReview()
                    event.accepted = true
                    return
                }
                if (!event.accepted)
                    disconnectReviewSpatialNav.handle(event)
            }
            Keys.onReleased: (event) => disconnectReviewSpatialNav.handleRelease(event)

            FocusScope {
                id: disconnectReviewContent
                anchors.centerIn: parent
                width: Math.min(570, root.width - 40)
                height: disconnectContent.implicitHeight + 52

                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: theme.biblioWashBottom
                    border.width: 1
                    border.color: theme.edge
                }

                ColumnLayout {
                    id: disconnectContent
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 14

                    Text {
                        objectName: "trackerDisconnectTitle"
                        text: root.disconnectCleanupMode()
                              ? "Finish removing known-unsent updates"
                              : "Disconnect " + (root.selectedDossier.providerName || "tracker") + "?"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 24
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        objectName: "trackerDisconnectDescription"
                        text: root.disconnectCleanupMode()
                              ? "This tracker is already disconnected. Retry removes only updates known not to have reached it. Any uncertain outcome remains paused. Colosseum's own History, Progress, and Activity are unchanged."
                              : "This disconnects the tracker from this Colosseum profile. It does not delete the tracker account or Colosseum's own History, Progress, or Activity. Imported tracker data remains here until you remove it separately."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerDisconnectPendingCounts"
                        text: "Pending: " + Number(root.selectedDossier.pendingCount || 0)
                              + " · Known unsent: "
                              + Number(root.selectedDossier.knownUnsentCount || 0)
                              + " · Unknown outcome: "
                              + Number(root.selectedDossier.unknownOutcomeCount || 0)
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerDisconnectRemoteAccessNotice"
                        visible: !root.disconnectCleanupMode()
                        text: "This removes the connection from Colosseum; it does not revoke access in the tracker account. Revoke access separately in that service's connected-app settings."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerDisconnectUnknownWarning"
                        visible: root.hasUncertainDelivery()
                        text: Number(root.selectedDossier.unknownOutcomeCount || 0)
                              + (Number(root.selectedDossier.unknownOutcomeCount || 0) === 1
                                 ? " update may already have reached the tracker. It cannot be discarded or resent while uncertain; it stays paused until reconnect so Colosseum can check first."
                                 : " updates may already have reached the tracker. They cannot be discarded or resent while uncertain; they stay paused until reconnect so Colosseum can check first.")
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerDisconnectDialogNotice"
                        visible: root.disconnectReviewNotice.length > 0
                        text: root.disconnectReviewNotice
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Button {
                        id: disconnectKeepPausedButton
                        objectName: "trackerDisconnectKeepPaused"
                        activeFocusOnTab: true
                        Layout.fillWidth: true
                        text: root.disconnectCleanupMode()
                              ? "Leave remaining work paused"
                              : "Disconnect and keep pending work"
                        Accessible.name: text + ". Pending tracker work stays paused for recovery."
                        KeyNavigation.tab: disconnectDiscardButton
                        KeyNavigation.backtab: disconnectCancelButton
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => root.handleDisconnectReviewTab(
                                            event, disconnectKeepPausedButton)
                        onClicked: root.confirmDisconnect("keep_paused")
                    }
                    Button {
                        id: disconnectDiscardButton
                        objectName: "trackerDisconnectDiscardKnownUnsent"
                        activeFocusOnTab: true
                        Layout.fillWidth: true
                        text: root.disconnectCleanupMode()
                              ? "Retry removing known-unsent updates"
                              : "Disconnect and discard known-unsent work"
                        Accessible.name: text + ". Uncertain deliveries are kept and remain paused."
                        KeyNavigation.tab: disconnectCancelButton
                        KeyNavigation.backtab: disconnectKeepPausedButton
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => root.handleDisconnectReviewTab(
                                            event, disconnectDiscardButton)
                        onClicked: root.confirmDisconnect("discard_known_unsent")
                    }
                    Button {
                        id: disconnectCancelButton
                        objectName: "trackerDisconnectCancel"
                        activeFocusOnTab: true
                        Layout.fillWidth: true
                        text: "Cancel"
                        Accessible.name: "Cancel disconnect"
                        KeyNavigation.tab: disconnectKeepPausedButton
                        KeyNavigation.backtab: disconnectDiscardButton
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => root.handleDisconnectReviewTab(
                                            event, disconnectCancelButton)
                        onClicked: root.closeDisconnectReview()
                    }
                }
            }

        }
    }

    Item {
        id: bulkImportReviewVeil
        objectName: "trackerImportBulkReviewVeil"
        anchors.fill: parent
        visible: root.bulkImportReviewOpen
        z: 35

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.72)
            MouseArea { anchors.fill: parent }
        }

        FocusScope {
            id: bulkImportReviewDialog
            objectName: "trackerImportBulkReviewDialog"
            anchors.fill: parent
            focus: visible
            activeFocusOnTab: true
            Accessible.role: Accessible.Dialog
            Accessible.name: "Review shared choices for selected tracker items"
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.closeBulkImportReview()
                    event.accepted = true
                }
            }

            FocusScope {
                anchors.centerIn: parent
                width: Math.min(650, root.width - 40)
                height: bulkImportContent.implicitHeight + 52

                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: theme.biblioWashBottom
                    border.width: 1
                    border.color: theme.edge
                }

                ColumnLayout {
                    id: bulkImportContent
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 14

                    Text {
                        objectName: "trackerImportBulkReviewTitle"
                        text: "Review choices for " + root.selectedBulkImportItemIds.length
                              + " selected items"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 24
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        objectName: "trackerImportBulkReviewTitles"
                        text: root.selectedBulkImportTitles().join(" · ")
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Only the selected items receive this decision. Other titles and any protected or unmatched exceptions remain visible for individual review. This saves decisions to the preview; no Progress or History is applied until the separate import confirmation."
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerImportBulkReviewNotice"
                        visible: root.bulkImportReviewNotice.length > 0
                        text: root.bulkImportReviewNotice
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        id: bulkImportChoiceRepeater
                        model: root.bulkImportCommonChoices()
                        delegate: Button {
                            required property string modelData
                            required property int index
                            objectName: "trackerImportBulkChoice_" + modelData
                            activeFocusOnTab: true
                            Layout.fillWidth: true
                            text: root.importChoiceLabel(modelData) + " for "
                                  + root.selectedBulkImportItemIds.length + " selected items"
                            Accessible.name: text + ". Only the selected titles will change."
                            KeyNavigation.backtab: index === 0
                                                   ? bulkImportReviewCancelButton
                                                   : bulkImportChoiceRepeater.itemAt(index - 1)
                            KeyNavigation.tab: index === bulkImportChoiceRepeater.count - 1
                                               ? bulkImportReviewCancelButton
                                               : bulkImportChoiceRepeater.itemAt(index + 1)
                            onClicked: root.confirmBulkImportChoice(modelData)
                        }
                    }
                    Button {
                        id: bulkImportReviewCancelButton
                        objectName: "trackerImportBulkReviewCancel"
                        activeFocusOnTab: true
                        Layout.fillWidth: true
                        text: "Cancel"
                        Accessible.name: "Cancel group decision and return to item review"
                        KeyNavigation.tab: bulkImportChoiceRepeater.itemAt(0)
                        KeyNavigation.backtab: bulkImportChoiceRepeater.itemAt(
                                                    bulkImportChoiceRepeater.count - 1)
                        onClicked: root.closeBulkImportReview()
                    }
                }
            }

        }
    }

    Item {
        id: importedHistoryReviewVeil
        objectName: "trackerRemoveImportedVeil"
        anchors.fill: parent
        visible: root.importedDataReviewOpen
        z: 45

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.68)
            MouseArea { anchors.fill: parent }
        }

        FocusScope {
            id: importedDataReviewDialog
            objectName: "trackerRemoveImportedDialog"
            anchors.fill: parent
            focus: visible
            activeFocusOnTab: true
            Accessible.role: Accessible.Dialog
            Accessible.name: "Review removal of tracker-imported History and Progress data"
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.closeImportedDataReview()
                    event.accepted = true
                }
            }

            FocusScope {
                anchors.centerIn: parent
                width: Math.min(590, root.width - 40)
                height: importedDataContent.implicitHeight + 52

                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: theme.biblioWashBottom
                    border.width: 1
                    border.color: theme.edge
                }

                ColumnLayout {
                    id: importedDataContent
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 14

                    Text {
                        objectName: "trackerRemoveImportedTitle"
                        text: "Remove imported data from "
                              + (root.selectedDossier.providerName || "this tracker") + "?"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 24
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        objectName: "trackerRemoveImportedCount"
                        text: Number(root.selectedDossier.importedHistoryCount || 0)
                              + " tracker-imported History "
                              + (Number(root.selectedDossier.importedHistoryCount || 0) === 1
                                 ? "item" : "items") + " and "
                              + Number(root.selectedDossier.importedProgressCount || 0)
                              + " tracker-imported Progress "
                              + (Number(root.selectedDossier.importedProgressCount || 0) === 1
                                 ? "item" : "items")
                              + " will be removed for this tracker account."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "trackerRemoveImportedProtection"
                        text: "Only this account's tracker-imported History evidence and eligible tracker-imported Progress are removed. Native History, Activity, statistics, local Progress, and another tracker's data are preserved. No remote tracker account or data is changed."
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Affected entries"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    ListView {
                        id: importedDataRemovalPreviewList
                        objectName: "trackerRemoveImportedPreview"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(150,
                            Math.max(48, count * 48))
                        clip: true
                        activeFocusOnTab: true
                        keyNavigationEnabled: true
                        Accessible.role: Accessible.List
                        Accessible.name: "Affected tracker-imported entries"
                        Accessible.description: "Use the arrow keys or scrolling to inspect every listed entry before confirming removal."
                        model: root.selectedDossier.importedDataRemovalPreview || []
                        boundsBehavior: Flickable.StopAtBounds
                        onActiveFocusChanged: {
                            if (activeFocus && currentIndex < 0 && count > 0)
                                currentIndex = 0
                        }
                        KeyNavigation.tab: removeImportedConfirmButton
                        KeyNavigation.backtab: removeImportedCancelButton
                        Keys.onPressed: (event) => {
                            var nextIndex = currentIndex < 0 ? 0 : currentIndex
                            if (event.key === Qt.Key_Down)
                                nextIndex = Math.min(count - 1, nextIndex + 1)
                            else if (event.key === Qt.Key_Up)
                                nextIndex = Math.max(0, nextIndex - 1)
                            else if (event.key === Qt.Key_PageDown)
                                nextIndex = Math.min(count - 1,
                                    nextIndex + Math.max(1, Math.floor(height / 48)))
                            else if (event.key === Qt.Key_PageUp)
                                nextIndex = Math.max(0,
                                    nextIndex - Math.max(1, Math.floor(height / 48)))
                            else if (event.key === Qt.Key_Home)
                                nextIndex = 0
                            else if (event.key === Qt.Key_End)
                                nextIndex = count - 1
                            else
                                return
                            if (count > 0) {
                                currentIndex = nextIndex
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                            event.accepted = true
                        }
                        delegate: Column {
                            required property string title
                            required property string dataKind
                            required property string consequence
                            required property int index
                            width: importedDataRemovalPreviewList.width
                            spacing: 3

                            Text {
                                objectName: parent.index === 0
                                             ? "trackerRemoveImportedPreviewTitle"
                                             : "trackerRemoveImportedPreviewTitle_" + parent.index
                                text: parent.title || "Untitled media"
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                width: parent.width
                                Accessible.name: text
                            }
                            Text {
                                text: (parent.dataKind || "Tracker data") + " · "
                                      + (parent.consequence || "Only this account's imported data is affected.")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }
                            Rectangle {
                                width: parent.width
                                height: 1
                                color: theme.edge
                            }
                        }
                    }
                    Text {
                        objectName: "trackerRemoveImportedNotice"
                        visible: root.importedDataReviewNotice.length > 0
                        text: root.importedDataReviewNotice
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Button {
                        id: removeImportedCancelButton
                        objectName: "trackerRemoveImportedCancel"
                        activeFocusOnTab: true
                        Layout.fillWidth: true
                        text: "Cancel"
                        Accessible.name: "Cancel removal; keep imported tracker data"
                        KeyNavigation.tab: importedDataRemovalPreviewList
                        KeyNavigation.backtab: removeImportedConfirmButton
                        onClicked: root.closeImportedDataReview()
                    }
                    Button {
                        id: removeImportedConfirmButton
                        objectName: "trackerRemoveImportedConfirm"
                        activeFocusOnTab: true
                        Layout.fillWidth: true
                        enabled: root.selectedDossier.removeImportedPending !== true
                        text: root.selectedDossier.removeImportedPending === true
                              ? "Removing imported data…" : "Remove imported data"
                        Accessible.name: root.selectedDossier.removeImportedPending === true
                                        ? "Removing tracker-imported History evidence and Progress"
                                        : "Remove the listed tracker-imported History evidence and Progress"
                        KeyNavigation.tab: removeImportedCancelButton
                        KeyNavigation.backtab: importedDataRemovalPreviewList
                        onClicked: root.confirmRemoveImportedData()
                    }
                }
            }

        }
    }

    Component.onCompleted: Qt.callLater(root.takeKeyboardFocus)
}
