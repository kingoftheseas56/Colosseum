import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../../qml" as Colosseum

TestCase {
    id: testCase
    name: "TrackerSyncCenterPage"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 820
        visible: true
    }

    QtObject {
        id: fixtureModel
        signal modelChanged()
        signal findMatchRequested(string batchId, string reviewItemId, int requestedRevision)
        property int revision: 1
        property var connectedTrackers: []
        property var catalogue: []
        property var globalSettings: ({
            trackerSyncEnabled: true,
            checkOnLaunch: true,
            backgroundDelivery: true,
            completionMessages: false,
            profileAvailable: true,
            editable: true
        })
        property var aggregateState: ({
            status: "Empty",
            healthy: true,
            ownerHealthy: true,
            ownerUnavailable: false,
            syncing: false,
            connectedCount: 0,
            waitingCount: 0,
            unresolvedCount: 0,
            attentionProviderCount: 0,
            canSyncAll: false
        })
        property var dossiers: ({})
        property var importReviews: []
        property var importSnapshot: ({ accepted: false, items: [] })
        property var exportSnapshot: ({ accepted: false, items: [] })
        property int exportBeginCalls: 0
        property int exportConfirmCalls: 0
        property var lastExportSelected: []
        property int providerReads: 0
        property int providerNetworkCalls: 0
        property int diagnoseRouteCalls: 0
        property string lastDiagnoseProviderKey: ""
        property var deliveryQueue: [{
            operationId: "opaque-delivery-1",
            title: "Frieren: Beyond Journey’s End",
            state: "checking_delivery",
            reason: "acknowledgement_lost",
            progress: 8,
            attemptCount: 1
        }]
        property bool reorderDeliveryBeforeRecoveryFocus: false
        property int deliveryReorderCalls: 0
        property int fixtureGeneration: 0
        property int importChoiceCalls: 0
        property int lastImportChoiceExpectedRevision: -1
        property string lastBulkChoice: ""
        property int importConfirmCalls: 0
        property int lastImportConfirmExpectedRevision: -1
        property int titleMatchCandidateReads: 0
        property int titleMatchConfirmCalls: 0
        property string titleMatchFailureCode: ""
        property int disconnectCalls: 0
        property int removeImportedCalls: 0
        property bool disconnectCleanupFailsOnce: false
        property bool removeImportedPendingScenario: false
        property string lastDisconnectProvider: ""
        property string lastDisconnectChoice: ""
        property var lastActionResult: ({ accepted: false, code: "idle" })

        function providerDossier(key) {
            providerReads++
            return dossiers[key] || ({ found: false, providerKey: key })
        }

        function deliveryRows(key) {
            return key === "trakt" ? deliveryQueue : []
        }

        function diagnoseRoute(key) {
            diagnoseRouteCalls++
            lastDiagnoseProviderKey = key
            var dossier = dossiers[key]
            if (key !== "trakt" || !dossier || dossier.status !== "Attention")
                return { accepted: false, code: "no_current_issue" }
            var route = {
                accepted: true,
                code: "current_issue",
                revision: revision,
                providerKey: key,
                destination: "provider_dossier",
                focusTarget: "delivery_status",
                state: deliveryQueue[0].state,
                reason: deliveryQueue[0].reason,
                focusIndex: 0
            }
            if (reorderDeliveryBeforeRecoveryFocus) {
                reorderDeliveryBeforeRecoveryFocus = false
                var reordered = deliveryQueue.slice().reverse()
                var scheduledGeneration = fixtureGeneration
                Qt.callLater(function() {
                    if (fixtureModel.fixtureGeneration !== scheduledGeneration)
                        return
                    fixtureModel.deliveryQueue = reordered
                    fixtureModel.deliveryReorderCalls++
                    fixtureModel.revision++
                    fixtureModel.modelChanged()
                })
            }
            return route
        }

        function setGlobalSetting(key, enabled, expectedRevision) {
            if (expectedRevision !== revision || globalSettings.editable !== true)
                return false
            var next = Object.assign({}, globalSettings)
            next[key] = enabled
            globalSettings = next
            revision++
            return true
        }

        function refresh() {}

        function beginExportReview(providerKey, expectedRevision) {
            exportBeginCalls++
            if (providerKey !== "trakt" || expectedRevision !== revision)
                return { accepted: false, code: "stale_intent", items: [] }
            return Object.assign({}, exportSnapshot, { revision: revision })
        }

        function confirmExportReview(reviewId, selectedItemIds, expectedRevision) {
            exportConfirmCalls++
            if (reviewId !== exportSnapshot.reviewId || expectedRevision !== revision
                    || !selectedItemIds || selectedItemIds.length !== 1
                    || selectedItemIds[0] !== "opaque-export-native")
                return false
            lastExportSelected = selectedItemIds.slice()
            dossiers = Object.assign({}, dossiers, {
                trakt: Object.assign({}, dossiers.trakt, {
                    exportReviewed: true, exportReviewEnabled: false,
                    sendSettingEnabled: true
                })
            })
            revision++
            modelChanged()
            return true
        }

        function importReviewSnapshot(batchId, expectedRevision) {
            if (expectedRevision !== revision || batchId !== importSnapshot.batchId)
                return { accepted: false, code: "stale_intent", items: [] }
            var snapshot = Object.assign({}, importSnapshot)
            snapshot.revision = revision
            return snapshot
        }

        function titleMatchCandidates(batchId, itemId, searchText, expectedRevision) {
            if (expectedRevision !== revision || batchId !== importSnapshot.batchId
                    || !itemId || itemId !== "opaque-item-match")
                return []
            titleMatchCandidateReads++
            var title = "Colosseum candidate title"
            if (searchText && title.toLowerCase().indexOf(searchText.toLowerCase()) < 0)
                return []
            return [{ candidateId: "opaque-title-candidate", displayName: title,
                      mediaType: "Video", displayContext: "Chapter 4" }]
        }

        function confirmTitleMatch(batchId, itemId, candidateId, expectedRevision) {
            if (expectedRevision !== revision || batchId !== importSnapshot.batchId
                    || itemId !== "opaque-item-match"
                    || candidateId !== "opaque-title-candidate")
                return false
            titleMatchConfirmCalls++
            if (titleMatchFailureCode.length) {
                if (titleMatchFailureCode !== "preview_refresh_failed_mapping_retained")
                    revision++
                lastActionResult = ({ accepted: false, action: "confirm_title_match",
                                      code: titleMatchFailureCode })
                modelChanged()
                return false
            }
            var next = Object.assign({}, importSnapshot)
            next.items = importSnapshot.items.slice()
            var row = Object.assign({}, next.items[0])
            row.classification = "unsupported"
            row.state = "no_change"
            row.allowedChoices = []
            next.items[0] = row
            importSnapshot = next
            revision++
            lastActionResult = ({ accepted: true, action: "confirm_title_match", code: "saved" })
            modelChanged()
            return true
        }

        function resolveImportItem(batchId, itemId, choice, expectedRevision) {
            lastImportChoiceExpectedRevision = expectedRevision
            if (expectedRevision !== revision || batchId !== importSnapshot.batchId)
                return false
            if (choice === "find_match") {
                if (itemId !== "opaque-item-match")
                    return false
                importChoiceCalls++
                findMatchRequested(batchId, itemId, revision)
                lastActionResult = ({ accepted: true, action: "find_match", code: "accepted" })
                modelChanged()
                return true
            }
            importChoiceCalls++
            var next = Object.assign({}, importSnapshot)
            next.items = importSnapshot.items.slice()
            var found = false
            for (var i = 0; i < next.items.length; ++i) {
                if (next.items[i].reviewItemId !== itemId)
                    continue
                var row = Object.assign({}, next.items[i])
                row.resolution = choice
                row.state = choice === "keep_colosseum" ? "kept_colosseum"
                            : choice === "use_provider_progress" ? "waiting_to_apply"
                            : "unresolved"
                row.allowedChoices = []
                next.items[i] = row
                found = true
                break
            }
            if (!found)
                return false
            importSnapshot = next
            var nextReviews = importReviews.slice()
            for (var reviewIndex = 0; reviewIndex < nextReviews.length; ++reviewIndex) {
                if (nextReviews[reviewIndex].batchId !== batchId)
                    continue
                var batch = Object.assign({}, nextReviews[reviewIndex])
                batch.decisionCount = Math.max(0, Number(batch.decisionCount || 0) - 1)
                if (choice === "leave_unmatched" || choice === "leave_unresolved")
                    batch.unresolvedCount = Number(batch.unresolvedCount || 0) + 1
                else if (choice === "use_provider_progress")
                    batch.awaitingApplyCount = Number(batch.awaitingApplyCount || 0) + 1
                nextReviews[reviewIndex] = batch
                break
            }
            importReviews = nextReviews
            revision++
            return true
        }

        function resolveImportItems(batchId, itemIds, choice, expectedRevision) {
            lastImportChoiceExpectedRevision = expectedRevision
            if (expectedRevision !== revision || batchId !== importSnapshot.batchId
                    || !itemIds || itemIds.length < 2)
                return false
            var next = Object.assign({}, importSnapshot)
            next.items = importSnapshot.items.slice()
            for (var i = 0; i < itemIds.length; ++i) {
                var found = false
                for (var j = 0; j < next.items.length; ++j) {
                    if (next.items[j].reviewItemId !== itemIds[i])
                        continue
                    var oldRow = next.items[j]
                    if (oldRow.state !== "review_required"
                            || (oldRow.allowedChoices || []).indexOf(choice) < 0)
                        return false
                    found = true
                }
                if (!found)
                    return false
            }
            lastBulkChoice = choice
            for (var k = 0; k < itemIds.length; ++k) {
                for (var r = 0; r < next.items.length; ++r) {
                    if (next.items[r].reviewItemId !== itemIds[k])
                        continue
                    var row = Object.assign({}, next.items[r])
                    row.resolution = choice
                    row.state = choice === "use_provider_progress" ? "waiting_to_apply"
                                : choice === "keep_colosseum" ? "kept_colosseum"
                                : "unresolved"
                    row.allowedChoices = []
                    next.items[r] = row
                    break
                }
            }
            importChoiceCalls += itemIds.length
            importSnapshot = next
            var nextReviews = importReviews.slice()
            for (var b = 0; b < nextReviews.length; ++b) {
                if (nextReviews[b].batchId !== batchId)
                    continue
                var summary = Object.assign({}, nextReviews[b])
                summary.decisionCount = Math.max(0, Number(summary.decisionCount || 0) - itemIds.length)
                if (choice === "use_provider_progress")
                    summary.awaitingApplyCount = Number(summary.awaitingApplyCount || 0) + itemIds.length
                else if (choice === "leave_unresolved")
                    summary.unresolvedCount = Number(summary.unresolvedCount || 0) + itemIds.length
                nextReviews[b] = summary
                break
            }
            importReviews = nextReviews
            revision++
            lastActionResult = ({ accepted: true, code: "saved" })
            return true
        }

        function confirmImport(batchId, expectedRevision) {
            lastImportConfirmExpectedRevision = expectedRevision
            if (expectedRevision !== revision || batchId !== importSnapshot.batchId)
                return false
            importConfirmCalls++
            var next = Object.assign({}, importSnapshot)
            next.confirmed = true
            importSnapshot = next
            var nextReviews = importReviews.slice()
            for (var i = 0; i < nextReviews.length; ++i) {
                if (nextReviews[i].batchId !== batchId)
                    continue
                var batch = Object.assign({}, nextReviews[i])
                batch.confirmed = true
                nextReviews[i] = batch
                break
            }
            importReviews = nextReviews
            revision++
            return true
        }

        function disconnectTracker(providerKey, choice, expectedRevision) {
            if (expectedRevision !== revision || providerKey !== "trakt")
                return false
            disconnectCalls++
            lastDisconnectProvider = providerKey
            lastDisconnectChoice = choice
            var nextDossiers = Object.assign({}, dossiers)
            if (choice === "discard_known_unsent" && disconnectCleanupFailsOnce) {
                disconnectCleanupFailsOnce = false
                nextDossiers[providerKey] = Object.assign({}, dossiers[providerKey], {
                    connected: false,
                    disconnectEnabled: false,
                    cleanupEnabled: true,
                    knownUnsentCount: 1,
                    status: "Disconnected"
                })
                dossiers = nextDossiers
                connectedTrackers = []
                catalogue = [{
                    providerKey: "trakt",
                    providerName: "Trakt",
                    status: "Attention",
                    capabilities: [],
                    available: true,
                    connected: false,
                    pendingWork: true,
                    connectEnabled: false
                }]
                aggregateState = Object.assign({}, aggregateState, {
                    status: "Attention",
                    healthy: false,
                    connectedCount: 0,
                    waitingCount: 1,
                    attentionProviderCount: 1,
                    canSyncAll: false
                })
                lastActionResult = ({ accepted: false, code: "disconnect_cleanup_pending" })
                revision++
                return false
            }
            nextDossiers[providerKey] = Object.assign({}, dossiers[providerKey], {
                connected: false,
                disconnectEnabled: false,
                cleanupEnabled: false,
                knownUnsentCount: 0,
                status: "Disconnected"
            })
            dossiers = nextDossiers
            connectedTrackers = []
            catalogue = [{
                providerKey: "trakt",
                providerName: "Trakt",
                status: "Attention",
                capabilities: [],
                available: true,
                connected: false,
                pendingWork: true,
                connectEnabled: false
            }, {
                providerKey: "mal",
                providerName: "MyAnimeList",
                status: "Unavailable in this build",
                capabilities: [],
                available: false,
                connected: false,
                connectEnabled: false
            }]
            aggregateState = Object.assign({}, aggregateState, {
                status: "Attention",
                healthy: false,
                connectedCount: 0,
                waitingCount: 1,
                attentionProviderCount: 1,
                canSyncAll: false
            })
            lastActionResult = ({ accepted: true, code: "disconnected_known_unsent_discarded" })
            revision++
            return true
        }

        function removeImportedData(providerKey, expectedRevision) {
            if (expectedRevision !== revision || providerKey !== "trakt")
                return false
            removeImportedCalls++
            if (removeImportedPendingScenario) {
                removeImportedPendingScenario = false
                var pendingDossiers = Object.assign({}, dossiers)
                pendingDossiers[providerKey] = Object.assign({}, dossiers[providerKey], {
                    removeImportedPending: true
                })
                dossiers = pendingDossiers
                lastActionResult = ({
                    accepted: true,
                    action: "remove_imported_tracker_data",
                    code: "removal_pending"
                })
                revision++
                modelChanged()
                return true
            }
            var nextDossiers = Object.assign({}, dossiers)
            nextDossiers[providerKey] = Object.assign({}, dossiers[providerKey], {
                importedHistoryCount: 0,
                importedProgressCount: 0,
                importedDataRemovalPreview: [],
                removeImportedPending: false,
                removeImportedEnabled: false
            })
            dossiers = nextDossiers
            lastActionResult = ({ accepted: true,
                                  action: "remove_imported_tracker_data",
                                  code: "removed" })
            revision++
            return true
        }

        function finishImportedRemoval(code) {
            var nextDossiers = Object.assign({}, dossiers)
            var current = Object.assign({}, dossiers.trakt, { removeImportedPending: false })
            if (code === "removed") {
                current.importedHistoryCount = 0
                current.importedProgressCount = 0
                current.importedDataRemovalPreview = []
                current.removeImportedEnabled = false
            } else if (code === "progress_removed_history_removal_failed") {
                current.importedProgressCount = 0
                current.importedDataRemovalPreview = (current.importedDataRemovalPreview || []).filter(
                    function(item) { return item.dataKind !== "Progress" })
            }
            nextDossiers.trakt = current
            dossiers = nextDossiers
            lastActionResult = ({ accepted: code === "removed",
                                  action: "remove_imported_tracker_data",
                                  code: code })
            revision++
            modelChanged()
        }
    }

    QtObject {
        id: otherProfileModel
        signal modelChanged()
        property int revision: 1
        property var connectedTrackers: []
        property var catalogue: []
        property var globalSettings: ({ profileAvailable: true, editable: true })
        property var aggregateState: ({ status: "Waiting", connectedCount: 1,
                                         waitingCount: 2, unresolvedCount: 0,
                                         attentionProviderCount: 0, canSyncAll: false })
        property var importReviews: []
        property var lastActionResult: ({ accepted: false, code: "idle" })

        function providerDossier(key) {
            if (key !== "trakt")
                return ({ found: false, providerKey: key })
            return ({ found: true, providerKey: "trakt", providerName: "Trakt",
                      available: true, connected: true,
                      accountLabel: "Connected account", status: "Waiting",
                      disconnectEnabled: true, cleanupEnabled: false,
                      removeImportedEnabled: true, importedHistoryCount: 2,
                      importedProgressCount: 1,
                      importedDataRemovalPreview: [
                          { title: "Other profile entry", dataKind: "Progress",
                            consequence: "Only this profile's imported Progress is removed." }
                      ] })
        }

        function deliveryRows(key) { return [] }
        function refresh() {}
    }

    Component {
        id: pageComponent
        Colosseum.TrackerSyncCenterPage {
            trackerModel: testCase.modelUnderTest
            width: testWindow.width
            height: testWindow.height
            visible: true
        }
    }

    property var page: null
    property var modelUnderTest: null
    SignalSpy { id: backSpy; signalName: "backRequested" }
    SignalSpy { id: mainSyncSpy; signalName: "mainSyncRequested" }

    function resetFixture() {
        fixtureModel.fixtureGeneration++
        fixtureModel.revision = 1
        fixtureModel.connectedTrackers = []
        fixtureModel.catalogue = []
        fixtureModel.globalSettings = ({
            trackerSyncEnabled: true,
            checkOnLaunch: true,
            backgroundDelivery: true,
            completionMessages: false,
            profileAvailable: true,
            editable: true
        })
        fixtureModel.aggregateState = ({
            status: "Empty",
            healthy: true,
            ownerHealthy: true,
            ownerUnavailable: false,
            syncing: false,
            connectedCount: 0,
            waitingCount: 0,
            unresolvedCount: 0,
            attentionProviderCount: 0,
            canSyncAll: false
        })
        fixtureModel.dossiers = ({})
        fixtureModel.importReviews = []
        fixtureModel.importSnapshot = ({ accepted: false, items: [] })
        fixtureModel.exportSnapshot = ({ accepted: false, items: [] })
        fixtureModel.exportBeginCalls = 0
        fixtureModel.exportConfirmCalls = 0
        fixtureModel.lastExportSelected = []
        fixtureModel.providerReads = 0
        fixtureModel.providerNetworkCalls = 0
        fixtureModel.diagnoseRouteCalls = 0
        fixtureModel.lastDiagnoseProviderKey = ""
        fixtureModel.deliveryQueue = [{
            operationId: "opaque-delivery-1",
            title: "Frieren: Beyond Journey’s End",
            state: "checking_delivery",
            reason: "acknowledgement_lost",
            progress: 8,
            attemptCount: 1
        }]
        fixtureModel.reorderDeliveryBeforeRecoveryFocus = false
        fixtureModel.deliveryReorderCalls = 0
        fixtureModel.importChoiceCalls = 0
        fixtureModel.lastImportChoiceExpectedRevision = -1
        fixtureModel.lastBulkChoice = ""
        fixtureModel.importConfirmCalls = 0
        fixtureModel.lastImportConfirmExpectedRevision = -1
        fixtureModel.titleMatchCandidateReads = 0
        fixtureModel.titleMatchConfirmCalls = 0
        fixtureModel.titleMatchFailureCode = ""
        fixtureModel.disconnectCalls = 0
        fixtureModel.removeImportedCalls = 0
        fixtureModel.disconnectCleanupFailsOnce = false
        fixtureModel.removeImportedPendingScenario = false
        fixtureModel.lastDisconnectProvider = ""
        fixtureModel.lastDisconnectChoice = ""
        fixtureModel.lastActionResult = ({ accepted: false, code: "idle" })
    }

    function init() {
        resetFixture()
        modelUnderTest = TrackerSyncCenter
        page = pageComponent.createObject(testWindow)
        verify(page !== null)
        backSpy.target = page
        mainSyncSpy.target = page
        backSpy.clear()
        mainSyncSpy.clear()
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true, 1000)
        page.takeKeyboardFocus()
        wait(0)
    }

    function cleanup() {
        backSpy.target = null
        mainSyncSpy.target = null
        if (page)
            page.destroy()
        page = null
        modelUnderTest = null
    }

    function findChild(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findChild(kids[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function activateTestWindow() {
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true, 1000)
    }

    function revealCatalogueCards() {
        var scroll = findChild(page, "trackerSyncCenterScroll")
        verify(scroll !== null)
        scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height)
        wait(0)
    }

    function revealScrollableItem(item) {
        var scroll = findChild(page, "trackerSyncCenterScroll")
        verify(scroll !== null)
        verify(item !== null)
        tryVerify(function() {
            var centerY = item.mapToItem(scroll.contentItem,
                                         item.width / 2, item.height / 2).y
            scroll.contentY = Math.max(0, Math.min(centerY - scroll.height / 2,
                                       scroll.contentHeight - scroll.height))
            var center = item.mapToItem(scroll, item.width / 2, item.height / 2)
            return center.y >= 0 && center.y <= scroll.height
        }, 1000)
        wait(40)
        activateTestWindow()
    }

    function revealDossierItem(item) {
        var scroll = findChild(page, "trackerDossierScroll")
        verify(scroll !== null)
        verify(item !== null)
        tryVerify(function() {
            var centerY = item.mapToItem(scroll.contentItem,
                                         item.width / 2, item.height / 2).y
            scroll.contentY = Math.max(0, Math.min(centerY - scroll.height / 2,
                                       scroll.contentHeight - scroll.height))
            var center = item.mapToItem(scroll, item.width / 2, item.height / 2)
            return center.y >= 0 && center.y <= scroll.height
        }, 1000)
        wait(40)
        activateTestWindow()
    }

    function useConnectedFixture() {
        // Each QML case shares this page instance. Do not carry a dossier
        // selection from the prior test into the connected-card journey.
        page.selectedProviderKey = ""
        page.selectedDossier = ({})
        modelUnderTest = fixtureModel
        fixtureModel.connectedTrackers = [{
            providerKey: "trakt",
            providerName: "Trakt",
            accountLabel: "Connected account",
            status: "Waiting",
            lastSuccessfulSyncAtMs: 0,
            pendingCount: 2,
            waitingCount: 2,
            unresolvedCount: 0,
            capabilities: ["read_history", "scrobble"],
            available: true,
            syncEnabled: false,
            disconnectEnabled: true
        }]
        fixtureModel.catalogue = [{
            providerKey: "mal",
            providerName: "MyAnimeList",
            status: "Unavailable in this build",
            capabilities: [],
            available: false,
            connected: false,
            connectEnabled: false
        }]
        fixtureModel.aggregateState = ({
            status: "Waiting",
            healthy: true,
            ownerHealthy: true,
            ownerUnavailable: false,
            syncing: false,
            connectedCount: 1,
            waitingCount: 2,
            unresolvedCount: 0,
            attentionProviderCount: 0,
            canSyncAll: false
        })
        fixtureModel.dossiers = ({
            trakt: {
                found: true,
                providerKey: "trakt",
                providerName: "Trakt",
                available: true,
                status: "Waiting",
                connected: true,
                accountLabel: "Connected account",
                capabilities: ["read_history", "scrobble"],
            waitingCount: 2,
            unresolvedCount: 0,
            knownUnsentCount: 1,
            pendingCount: 2,
            unknownOutcomeCount: 1,
            importedHistoryCount: 2,
            importedProgressCount: 1,
            importedDataRemovalPreview: [
                { title: "Frieren", dataKind: "Completion evidence",
                  consequence: "Only this tracker's History evidence is removed; native History and other tracker evidence remain." },
                { title: "Frieren", dataKind: "Activity evidence",
                  consequence: "Only this tracker's History evidence is removed; native History and other tracker evidence remain." },
                { title: "Frieren", dataKind: "Progress",
                  consequence: "Your local Progress will be restored." }
            ],
            removeImportedEnabled: true,
                syncEnabled: false,
                disconnectEnabled: true,
                cleanupEnabled: false
            },
            mal: {
                found: true,
                providerKey: "mal",
                providerName: "MyAnimeList",
                available: false,
                status: "Unavailable in this build",
                connected: false,
                accountLabel: "",
                capabilities: [],
                waitingCount: 0,
                unresolvedCount: 0,
                connectEnabled: false
            }
        })
        fixtureModel.importReviews = [{
            batchId: "opaque-batch-1",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            reviewCount: 2,
            decisionCount: 2,
            awaitingApplyCount: 0,
            unresolvedCount: 0,
            pageComplete: true,
            confirmed: false
        }]
        fixtureModel.importSnapshot = {
            accepted: true,
            code: "ready",
            batchId: "opaque-batch-1",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            pageComplete: true,
            confirmed: false,
            items: [
                {
                    reviewItemId: "opaque-item-1",
                    title: "Frieren: Beyond Journey’s End",
                    classification: "disagreement",
                    state: "review_required",
                    matched: true,
                    providerProgress: 12,
                    providerCompleted: false,
                    hasLocalAtPreview: true,
                    localProgress: 8,
                    localCompleted: false,
                    nativeHistoryProtected: true,
                    allowedChoices: ["keep_colosseum"]
                },
                {
                    reviewItemId: "opaque-item-2",
                    title: "Unknown anime title",
                    classification: "needs_matching",
                    state: "review_required",
                    matched: false,
                    providerProgress: 3,
                    providerCompleted: false,
                    hasLocalAtPreview: false,
                    localProgress: -1,
                    localCompleted: false,
                    nativeHistoryProtected: false,
                    allowedChoices: ["leave_unmatched"]
                }
            ]
        }
    }

    function useAttentionFixture() {
        useConnectedFixture()
        fixtureModel.connectedTrackers = [Object.assign({},
            fixtureModel.connectedTrackers[0], { status: "Attention" })]
        fixtureModel.aggregateState = Object.assign({}, fixtureModel.aggregateState, {
            status: "Attention",
            unresolvedCount: 1,
            attentionProviderCount: 1
        })
        fixtureModel.dossiers = Object.assign({}, fixtureModel.dossiers, {
            trakt: Object.assign({}, fixtureModel.dossiers.trakt, { status: "Attention" })
        })
    }

    function openConnectedDossier() {
        useConnectedFixture()
        verify(page.openDossier("trakt"))
        tryCompare(page, "dossierOpen", true, 1000)
    }

    function useBulkImportFixture() {
        useConnectedFixture()
        fixtureModel.importReviews = [{
            batchId: "opaque-batch-bulk-guard",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            reviewCount: 2,
            decisionCount: 2,
            awaitingApplyCount: 0,
            unresolvedCount: 0,
            pageComplete: true,
            confirmed: false
        }]
        fixtureModel.importSnapshot = {
            accepted: true,
            code: "ready",
            batchId: "opaque-batch-bulk-guard",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            pageComplete: true,
            confirmed: false,
            items: [{
                reviewItemId: "bulk-guard-a",
                title: "Safe new title A",
                classification: "new_progress",
                state: "review_required",
                matched: true,
                providerProgress: 4,
                providerCompleted: false,
                hasLocalAtPreview: false,
                nativeHistoryProtected: false,
                allowedChoices: ["use_provider_progress", "leave_unresolved"]
            }, {
                reviewItemId: "bulk-guard-b",
                title: "Safe new title B",
                classification: "remote_advance",
                state: "review_required",
                matched: true,
                providerProgress: 7,
                providerCompleted: false,
                hasLocalAtPreview: true,
                localProgress: 3,
                localCompleted: false,
                nativeHistoryProtected: false,
                allowedChoices: ["use_provider_progress", "leave_unresolved"]
            }]
        }
    }

    function openFixtureBulkImportReview() {
        wait(0)
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerImportReviewButton_opaque-batch-bulk-guard"))
        wait(0)
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-guard-a"))
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-guard-b"))
        mouseClick(findChild(page, "trackerImportBulkReviewButton"))
        tryCompare(page, "bulkImportReviewOpen", true, 1000)
    }

    function openFixtureImportReview(batchId) {
        wait(0)
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerImportReviewButton_" + batchId))
        tryCompare(page, "importReviewOpen", true, 1000)
    }

    function test_empty_state_uses_native_unavailable_catalogue() {
        verify(findChild(page, "trackerEmptyState") !== null)
        compare(findChild(page, "trackerEmptyState").visible, true)
        compare(findChild(page, "trackerConnectedSection").visible, false)
        compare(TrackerSyncCenter.connectedTrackers.length, 0)
        compare(TrackerSyncCenter.catalogue.length, 4)
        compare(findChild(page, "trackerSyncAllButton").enabled, false)

        var dossierButton = findChild(page, "trackerCatalogue_simkl")
        var providerIcon = findChild(page, "trackerProviderIcon_simkl")
        verify(dossierButton !== null)
        verify(providerIcon !== null)
        revealCatalogueCards()
        compare(providerIcon.status, Image.Ready)
        mouseClick(dossierButton)
        wait(0)
        compare(findChild(page, "trackerDossierStatus").text,
                "Unavailable in this build")
        compare(findChild(page, "trackerConnectButton").enabled, false)
        compare(page.selectedProviderKey, "simkl")
    }

    function test_connection_heading_and_summary_scope_third_party_trackers() {
        compare(findChild(page, "trackerSyncPageTitle").text, "Connections")
        page.stremioState = ({ linkedAccount: true })
        compare(page.stremioConnected, true)
        compare(findChild(page, "trackerRelayStatus").text,
                "No third-party trackers connected")
    }

    function test_native_and_stremio_cards_share_the_house_layout() {
        compare(findChild(page, "trackerExternalServicesTitle").text,
                "External services")
        compare(findChild(page, "trackerExternalServicesDescription").text,
                "Manage connections to external libraries.")
        compare(findChild(page, "trackerCatalogueTitle").text, "Available trackers")

        var nativeCard = findChild(page, "trackerEmptyState")
        var stremioCard = findChild(page, "trackerStremioCard")
        var scroll = findChild(page, "trackerSyncCenterScroll")
        verify(nativeCard !== null)
        verify(stremioCard !== null)
        verify(scroll !== null)
        compare(nativeCard.color, stremioCard.color)
        compare(Math.round(page.height - scroll.height), page.taskbarBottomClearance)

        var stremioIcon = findChild(page, "trackerStremioIcon")
        verify(stremioIcon !== null)
        tryCompare(stremioIcon, "status", Image.Ready, 1000)
        verify(stremioIcon.source.toString().indexOf("stremio-official.svg") >= 0)

        var labels = ["Library", "Progress", "History"]
        for (var i = 0; i < labels.length; ++i) {
            var key = labels[i].toLowerCase()
            var tag = findChild(page, "trackerStremioCapabilityTag_" + key)
            var label = findChild(page, "trackerStremioCapabilityLabel_" + key)
            verify(tag !== null)
            verify(label !== null)
            compare(label.text, labels[i])
            compare(tag.implicitHeight, 21)
            compare(tag.border.width, 0)
        }

        var status = findChild(page, "trackerStremioStatusBadge")
        var settings = findChild(page, "trackerMainSyncButton")
        verify(status !== null)
        verify(settings !== null)
        var statusRight = status.mapToItem(stremioCard, 0, 0).x + status.width
        var settingsRight = settings.mapToItem(stremioCard, 0, 0).x + settings.width
        compare(Math.round(statusRight), Math.round(settingsRight))
    }

    function test_connected_cards_precede_catalogue_and_keep_capability_truth() {
        useConnectedFixture()
        wait(0)
        compare(page.connectedCount, 1)
        compare(page.waitingCount, 2)
        compare(findChild(page, "trackerConnectedGrid").children.length > 0, true)
        verify(findChild(page, "trackerCard_trakt") !== null)
        var connectedIcon = findChild(page, "trackerConnectedIcon_trakt")
        verify(connectedIcon !== null)
        compare(connectedIcon.status, Image.Ready)
        verify(findChild(page, "trackerCatalogue_mal") !== null)
        compare(findChild(page, "trackerCatalogue_mal").enabled, true)

        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        compare(page.dossierOpen, true)
        compare(findChild(page, "trackerDossierStatus").text, "Waiting")
        compare(page.dossierCapabilities.length, 2)
        compare(findChild(page, "trackerDeliveryQueueSection").visible, true)
        compare(findChild(page, "trackerDeliveryState").text, "Checking delivery")
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function test_attention_card_routes_to_current_delivery_issue_without_provider_work() {
        useAttentionFixture()
        wait(0)
        var card = findChild(page, "trackerCard_trakt")
        verify(card !== null)
        revealScrollableItem(card)
        mouseClick(card)

        compare(fixtureModel.diagnoseRouteCalls, 1)
        compare(fixtureModel.lastDiagnoseProviderKey, "trakt")
        compare(page.dossierOpen, true)
        compare(page.recoveryRouteActive, true)
        compare(page.recoveryRouteProviderKey, "trakt")
        compare(page.recoveryRouteFocusIndex, 0)
        var issueRow = findChild(page, "trackerDeliveryStatusRow_0")
        verify(issueRow !== null)
        tryCompare(issueRow, "activeFocus", true, 1000)
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function test_attention_route_drops_focus_when_queue_reorders_before_focus() {
        useAttentionFixture()
        fixtureModel.deliveryQueue = [{
            operationId: "opaque-delivery-first",
            title: "First playback item",
            state: "checking_delivery",
            reason: "acknowledgement_lost",
            progress: 8,
            attemptCount: 1
        }, {
            operationId: "opaque-delivery-second",
            title: "Second playback item",
            state: "checking_delivery",
            reason: "acknowledgement_lost",
            progress: 4,
            attemptCount: 1
        }]
        fixtureModel.reorderDeliveryBeforeRecoveryFocus = true
        wait(0)

        var card = findChild(page, "trackerCard_trakt")
        verify(card !== null)
        verify(page.openRecoveryRoute("trakt", card))

        tryCompare(page, "dossierOpen", true, 1000)
        compare(fixtureModel.diagnoseRouteCalls, 1)
        tryCompare(fixtureModel, "deliveryReorderCalls", 1, 1000)
        compare(fixtureModel.deliveryQueue[0].title, "Second playback item")
        compare(page.selectedDossier.deliveryRows[0].title, "Second playback item")
        tryCompare(page, "recoveryRouteActive", false, 1000)
        var row = findChild(page, "trackerDeliveryStatusRow_0")
        verify(row !== null)
        compare(row.modelData.title, "Second playback item")
        compare(row.activeFocus, false)
        var closeButton = findChild(page, "trackerDossierClose")
        verify(closeButton !== null)
        tryCompare(closeButton, "activeFocus", true, 1000)
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function test_preferences_persist_without_starting_provider_work() {
        useConnectedFixture()
        wait(0)
        mouseClick(findChild(page, "trackerPreferencesButton"))
        wait(0)
        compare(page.selectedProviderKey, "global")
        compare(findChild(page, "trackerDossierTitle").text, "Connection preferences")
        var syncToggle = findChild(page, "trackerSetting_trackerSyncEnabled")
        var completionToggle = findChild(page, "trackerSetting_completionMessages")
        compare(syncToggle.checked, true)
        compare(completionToggle.checked, false)
        compare(syncToggle.enabled, true)
        mouseClick(syncToggle)
        wait(0)
        compare(fixtureModel.globalSettings.trackerSyncEnabled, false)
        compare(syncToggle.checked, false)
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function useExportFixture() {
        useConnectedFixture()
        var dossier = Object.assign({}, fixtureModel.dossiers.trakt, {
            capabilities: ["read_history", "write_progress"],
            exportReviewed: false,
            exportReviewEnabled: true,
            sendSettingEnabled: false
        })
        fixtureModel.dossiers = Object.assign({}, fixtureModel.dossiers, { trakt: dossier })
        fixtureModel.exportSnapshot = {
            accepted: true, code: "ready", reviewId: "opaque-export-review",
            providerName: "Trakt", eligibleCount: 1,
            items: [
                { itemId: "opaque-export-native", title: "Local title",
                  kind: "Progress", progress: 8, eligible: true,
                  willChangeRemote: true, remoteBefore: "Progress: 12 episodes",
                  remoteAfter: "Progress: 8",
                  reason: "Ready to send" },
                { itemId: "opaque-export-imported", title: "Imported title",
                  kind: "Progress", progress: 3, eligible: false,
                  reason: "Imported progress stays in Colosseum" }
            ]
        }
        verify(page.openDossier("trakt"))
        tryCompare(page, "dossierOpen", true, 1000)
    }

    function test_export_review_keyboard_selects_only_native_and_confirms_separately() {
        useExportFixture()
        var openButton = findChild(page, "trackerExportReviewButton")
        verify(openButton !== null)
        compare(openButton.enabled, true)
        verify(page.openExportReview())
        wait(0)
        compare(fixtureModel.exportBeginCalls, 1)
        compare(findChild(page, "trackerExportReviewVeil").visible, true)
        var closeButton = findChild(page, "trackerExportReviewClose")
        var list = findChild(page, "trackerExportReviewItems")
        var confirmButton = findChild(page, "trackerExportReviewConfirm")
        compare(confirmButton.enabled, false)
        compare(findChild(page, "trackerExportReviewItem_opaque-export-imported").enabled, false)
        closeButton.forceActiveFocus()
        activateTestWindow()
        keyClick(Qt.Key_Tab)
        tryCompare(list, "activeFocus", true, 1000)
        list.currentIndex = 0
        keyClick(Qt.Key_Space)
        compare(page.selectedExportItemIds.length, 1)
        var nativeRow = findChild(page, "trackerExportReviewItem_opaque-export-native")
        verify(nativeRow.text.indexOf("Progress: 12 episodes") >= 0)
        verify(nativeRow.text.indexOf("after send: Progress: 8") >= 0)
        compare(nativeRow.checked, true)
        mouseClick(nativeRow)
        wait(0)
        compare(nativeRow.checked, false)
        compare(page.selectedExportItemIds.length, 0)
        mouseClick(nativeRow)
        wait(0)
        compare(nativeRow.checked, true)
        compare(confirmButton.enabled, true)
        activateTestWindow()
        keyClick(Qt.Key_Tab)
        tryCompare(confirmButton, "activeFocus", true, 1000)
        keyClick(Qt.Key_Space)
        wait(0)
        compare(fixtureModel.exportConfirmCalls, 1)
        compare(fixtureModel.lastExportSelected[0], "opaque-export-native")
        compare(fixtureModel.importConfirmCalls, 0)
        compare(findChild(page, "trackerExportReviewVeil").visible, false)
    }

    function test_export_review_stale_revision_and_escape_keep_consent_untouched() {
        useExportFixture()
        var openButton = findChild(page, "trackerExportReviewButton")
        verify(openButton !== null)
        verify(page.openExportReview())
        tryCompare(findChild(page, "trackerExportReviewVeil"), "visible", true, 1000)
        fixtureModel.revision++
        fixtureModel.modelChanged()
        wait(0)
        compare(findChild(page, "trackerExportReviewVeil").visible, false)
        compare(fixtureModel.exportConfirmCalls, 0)
        compare(fixtureModel.dossiers.trakt.exportReviewed, false)

        verify(page.openExportReview())
        var closeButton = findChild(page, "trackerExportReviewClose")
        closeButton.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        wait(0)
        compare(findChild(page, "trackerExportReviewVeil").visible, false)
        compare(page.dossierOpen, true)
        compare(fixtureModel.exportConfirmCalls, 0)
    }

    function test_export_review_with_no_eligible_items_explains_and_cannot_confirm() {
        useExportFixture()
        fixtureModel.exportSnapshot = Object.assign({}, fixtureModel.exportSnapshot, {
            eligibleCount: 0,
            items: [{ itemId: "opaque-export-imported", title: "Imported title",
                      kind: "Progress", progress: 3, eligible: false,
                      reason: "Imported progress stays in Colosseum" }]
        })
        var openButton = findChild(page, "trackerExportReviewButton")
        revealDossierItem(openButton)
        verify(page.openExportReview())
        wait(0)
        compare(findChild(page, "trackerExportReviewVeil").visible, true)
        compare(findChild(page, "trackerExportReviewEmptyNotice").visible, true)
        compare(findChild(page, "trackerExportReviewConfirm").enabled, false)
        compare(findChild(page, "trackerExportReviewItem_opaque-export-imported").enabled, false)
        compare(fixtureModel.exportConfirmCalls, 0)
    }

    function test_import_preview_requires_item_decisions_and_never_claims_native_history() {
        useConnectedFixture()
        wait(0)
        var providerCard = findChild(page, "trackerCard_trakt")
        verify(providerCard !== null)
        mouseClick(providerCard)
        wait(0)
        var reviewButton = findChild(page, "trackerImportReviewButton_opaque-batch-1")
        verify(reviewButton !== null)
        mouseClick(reviewButton)
        wait(0)

        compare(findChild(page, "trackerImportBatchSummary").text, "2 items need a decision")
        compare(findChild(page, "trackerImportReviewVeil").visible, true)
        verify(findChild(page, "trackerImportHistoryProtection") !== null)
        var footerClose = findChild(page, "trackerCancelImportReviewButton")
        var headerClose = findChild(page, "trackerImportReviewClose")
        footerClose.forceActiveFocus()
        tryCompare(footerClose, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(headerClose, "activeFocus", true, 1000)
        keyClick(Qt.Key_Backtab)
        tryCompare(footerClose, "activeFocus", true, 1000)
        compare(findChild(page, "trackerConfirmImportButton").enabled, false)
        verify(findChild(page, "trackerImportChoice_opaque-item-1_keep_colosseum") !== null)
        verify(findChild(page, "trackerImportChoice_opaque-item-2_leave_unmatched") !== null)
        compare(findChild(page, "trackerImportChoice_opaque-item-1_use_provider_progress"), null)
        verify(findChild(page, "trackerImportChoice_opaque-item-2_find_match") === null)

        mouseClick(findChild(page, "trackerImportChoice_opaque-item-1_keep_colosseum"))
        wait(0)
        compare(findChild(page, "trackerConfirmImportButton").enabled, false)
        tryCompare(findChild(page, "trackerImportChoice_opaque-item-2_leave_unmatched"),
                   "activeFocus", true, 1000)
        mouseClick(findChild(page, "trackerImportChoice_opaque-item-2_leave_unmatched"))
        wait(0)
        compare(findChild(page, "trackerConfirmImportButton").enabled, true)
        tryCompare(findChild(page, "trackerConfirmImportButton"), "activeFocus", true, 1000)
        verify(findChild(page, "trackerImportDecisionStatus_opaque-item-2").text.indexOf("Left unmatched") >= 0)
        mouseClick(findChild(page, "trackerConfirmImportButton"))
        wait(0)
        compare(fixtureModel.importConfirmCalls, 1)
        compare(fixtureModel.importSnapshot.confirmed, true)
        tryCompare(findChild(page, "trackerCancelImportReviewButton"), "activeFocus", true, 1000)
        compare(findChild(page, "trackerImportReviewNotice").text.indexOf("History") >= 0, true)
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function test_import_choice_rejects_a_revision_newer_than_the_displayed_preview() {
        useConnectedFixture()
        openFixtureImportReview("opaque-batch-1")

        var choice = findChild(page, "trackerImportChoice_opaque-item-1_keep_colosseum")
        verify(choice !== null)
        compare(choice.enabled, true)
        var displayedRevision = page.importReviewRevision()
        compare(displayedRevision, fixtureModel.revision)

        fixtureModel.revision++
        compare(choice.enabled, false)
        compare(page.resolveImportChoice(fixtureModel.importSnapshot.items[0],
                                         "keep_colosseum"), false)
        compare(fixtureModel.importChoiceCalls, 0)
        compare(fixtureModel.lastImportChoiceExpectedRevision, -1)
        compare(fixtureModel.importSnapshot.items[0].state, "review_required")
        compare(page.importReviewRevision(), fixtureModel.revision)
        compare(page.importReviewIsCurrent(), true)
    }

    function test_import_confirmation_rejects_a_revision_newer_than_the_displayed_preview() {
        useConnectedFixture()
        var resolvedRows = []
        for (var i = 0; i < fixtureModel.importSnapshot.items.length; ++i) {
            resolvedRows.push(Object.assign({}, fixtureModel.importSnapshot.items[i], {
                state: i === 0 ? "kept_colosseum" : "unresolved",
                resolution: i === 0 ? "keep_colosseum" : "leave_unmatched",
                allowedChoices: []
            }))
        }
        fixtureModel.importSnapshot = Object.assign({}, fixtureModel.importSnapshot, {
            items: resolvedRows
        })
        openFixtureImportReview("opaque-batch-1")

        var confirm = findChild(page, "trackerConfirmImportButton")
        verify(confirm !== null)
        compare(confirm.enabled, true)
        var displayedRevision = page.importReviewRevision()
        compare(displayedRevision, fixtureModel.revision)

        fixtureModel.revision++
        compare(confirm.enabled, false)
        compare(page.confirmImportReview(), false)
        compare(fixtureModel.importConfirmCalls, 0)
        compare(fixtureModel.lastImportConfirmExpectedRevision, -1)
        compare(fixtureModel.importSnapshot.confirmed, false)
        compare(page.importReviewRevision(), fixtureModel.revision)
        compare(page.importReviewCanConfirm(), true)
    }

    function test_find_match_saves_mapping_without_changing_progress_or_history() {
        useConnectedFixture()
        fixtureModel.importReviews = [{
            batchId: "opaque-batch-match",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            reviewCount: 1,
            decisionCount: 1,
            awaitingApplyCount: 0,
            unresolvedCount: 0,
            pageComplete: true,
            confirmed: false
        }]
        fixtureModel.importSnapshot = {
            accepted: true,
            code: "ready",
            batchId: "opaque-batch-match",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            pageComplete: true,
            confirmed: false,
            items: [{
                reviewItemId: "opaque-item-match",
                title: "Tracker supplied title",
                classification: "needs_matching",
                state: "review_required",
                matched: false,
                providerProgress: 8,
                providerCompleted: false,
                hasLocalAtPreview: false,
                localProgress: -1,
                localCompleted: false,
                nativeHistoryProtected: false,
                allowedChoices: ["find_match", "leave_unmatched"]
            }]
        }
        wait(0)
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerImportReviewButton_opaque-batch-match"))
        wait(0)

        var findButton = findChild(page, "trackerImportChoice_opaque-item-match_find_match")
        verify(findButton !== null)
        mouseClick(findButton)
        wait(0)
        var finder = findChild(page, "trackerTitleMatchVeil")
        compare(finder.visible, true)
        compare(findChild(page, "trackerImportReviewVeil").visible, true)
        verify(findChild(page, "trackerTitleMatchExplanation").text.indexOf(
                   "Tracker supplied title") >= 0)
        var search = findChild(page, "trackerTitleMatchSearch")
        tryCompare(search, "activeFocus", true, 1000)
        var searchKeys = [Qt.Key_M, Qt.Key_I, Qt.Key_S, Qt.Key_S,
                          Qt.Key_I, Qt.Key_N, Qt.Key_G]
        for (var searchIndex = 0; searchIndex < searchKeys.length; ++searchIndex)
            keyClick(searchKeys[searchIndex])
        wait(0)
        compare(findChild(page, "trackerTitleMatchCandidates").count, 0)
        compare(findChild(page, "trackerTitleMatchEmptyState").visible, true)
        compare(findChild(page, "trackerTitleMatchEmptyState").text,
                "No selectable matching Colosseum Progress titles.")

        keyClick(Qt.Key_Escape)
        wait(0)
        compare(finder.visible, false)
        compare(findChild(page, "trackerImportReviewVeil").visible, true)
        tryCompare(findButton, "activeFocus", true, 1000)
        mouseClick(findButton)
        wait(0)
        finder = findChild(page, "trackerTitleMatchVeil")
        compare(finder.visible, true)
        search = findChild(page, "trackerTitleMatchSearch")
        tryCompare(search, "activeFocus", true, 1000)
        var candidate = findChild(page, "trackerTitleMatchCandidate_opaque-title-candidate")
        verify(candidate !== null)
        compare(candidate.Accessible.name,
                "Colosseum candidate title, Video, Chapter 4, save title match")
        compare(findChild(page, "trackerTitleMatchCandidateMetadata_opaque-title-candidate").text,
                "Video · Chapter 4")
        fixtureModel.titleMatchFailureCode = "preview_refresh_failed_mapping_retained"
        candidate.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(candidate, "activeFocus", true, 1000)
        keyClick(Qt.Key_Space)
        wait(0)

        compare(page.titleMatchOpen, true)
        compare(fixtureModel.titleMatchConfirmCalls, 1)
        verify(findChild(page, "trackerTitleMatchNotice").text.indexOf(
                   "match is saved, but the review could not refresh") >= 0)
        verify(findChild(page, "trackerTitleMatchNotice").text.indexOf(
                   "Progress and History were not changed") >= 0)
        compare(fixtureModel.revision, page.importReviewRevision())
        compare(findChild(page, "trackerTitleMatchCandidates").count, 0)
        var candidateReadsBeforeStaleSearch = fixtureModel.titleMatchCandidateReads
        search.forceActiveFocus()
        tryCompare(search, "activeFocus", true, 1000)
        keyClick(Qt.Key_A)
        wait(0)
        compare(page.titleMatchSearchText, "a")
        compare(findChild(page, "trackerTitleMatchCandidates").count, 0)
        compare(fixtureModel.titleMatchCandidateReads, candidateReadsBeforeStaleSearch)
        verify(findChild(page, "trackerTitleMatchNotice").text.indexOf(
                   "Reopen the review before choosing another match") >= 0)
        compare(page.confirmTitleMatchCandidate("opaque-title-candidate"), false)
        compare(fixtureModel.titleMatchConfirmCalls, 1)
        fixtureModel.titleMatchFailureCode = ""
        keyClick(Qt.Key_Escape)
        wait(0)
        compare(finder.visible, false)
        tryVerify(function() {
            return page.selectedImportSnapshot.revision === fixtureModel.revision
        }, 1000)
        mouseClick(findButton)
        wait(0)
        finder = findChild(page, "trackerTitleMatchVeil")
        compare(finder.visible, true)
        search = findChild(page, "trackerTitleMatchSearch")
        tryCompare(search, "activeFocus", true, 1000)
        candidate = findChild(page, "trackerTitleMatchCandidate_opaque-title-candidate")
        verify(candidate !== null)
        candidate.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(candidate, "activeFocus", true, 1000)
        keyClick(Qt.Key_Space)
        wait(0)

        compare(fixtureModel.titleMatchConfirmCalls, 2)
        compare(fixtureModel.importConfirmCalls, 0)
        compare(fixtureModel.providerNetworkCalls, 0)
        compare(page.titleMatchOpen, false)
        compare(page.importReviewOpen, true)
        compare(fixtureModel.importSnapshot.items[0].classification, "unsupported")
        compare(fixtureModel.importSnapshot.items[0].state, "no_change")
        compare(findChild(page, "trackerImportDecisionStatus_opaque-item-match").text.indexOf(
                   "Progress and History stayed unchanged") >= 0, true)
        compare(findChild(page, "trackerImportReviewNotice").text.indexOf(
                   "verified exact Progress target") >= 0, true)
        compare(findChild(page, "trackerImportReviewNotice").text.indexOf(
                   "History remain unchanged") >= 0, true)
    }

    function test_resolved_unconfirmed_import_can_be_reopened_and_confirmed() {
        useConnectedFixture()
        fixtureModel.importReviews = [{
            batchId: "opaque-batch-resolved",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            reviewCount: 1,
            decisionCount: 1,
            awaitingApplyCount: 0,
            unresolvedCount: 0,
            pageComplete: true,
            confirmed: false
        }]
        fixtureModel.importSnapshot = {
            accepted: true,
            code: "ready",
            batchId: "opaque-batch-resolved",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            pageComplete: true,
            confirmed: false,
            items: [{
                reviewItemId: "opaque-item-resolved",
                title: "Frieren: Beyond Journey’s End",
                classification: "new_progress",
                state: "review_required",
                matched: true,
                providerProgress: 8,
                providerCompleted: false,
                hasLocalAtPreview: false,
                localProgress: -1,
                localCompleted: false,
                nativeHistoryProtected: false,
                allowedChoices: ["use_provider_progress", "leave_unresolved"]
            }]
        }
        wait(0)
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        var reviewButton = findChild(page, "trackerImportReviewButton_opaque-batch-resolved")
        mouseClick(reviewButton)
        wait(0)
        compare(findChild(page, "trackerImportBatchSummary").text, "1 item needs a decision")
        mouseClick(findChild(page, "trackerImportChoice_opaque-item-resolved_leave_unresolved"))
        wait(0)
        compare(findChild(page, "trackerImportDecisionStatus_opaque-item-resolved").text,
                "Left for later · Colosseum progress was not changed")
        compare(findChild(page, "trackerImportBatchSummary").text,
                "1 item left unchanged · Ready to confirm")
        tryCompare(findChild(page, "trackerConfirmImportButton"), "activeFocus", true, 1000)
        compare(findChild(page, "trackerImportReviewButton_opaque-batch-resolved").visible, true)
        mouseClick(findChild(page, "trackerCancelImportReviewButton"))
        wait(0)
        compare(page.importReviewOpen, false)
        tryCompare(findChild(page, "trackerImportReviewButton_opaque-batch-resolved"),
                   "activeFocus", true, 1000)
        verify(findChild(page, "trackerImportReviewButton_opaque-batch-resolved") !== null)
        mouseClick(findChild(page, "trackerImportReviewButton_opaque-batch-resolved"))
        wait(0)
        compare(findChild(page, "trackerConfirmImportButton").enabled, true)
        verify(findChild(page, "trackerImportReviewItems").count === 1)
        compare(fixtureModel.importConfirmCalls, 0)
        mouseClick(findChild(page, "trackerConfirmImportButton"))
        wait(0)
        compare(fixtureModel.importConfirmCalls, 1)
        compare(fixtureModel.importSnapshot.confirmed, true)
        compare(findChild(page, "trackerImportBatchSummary").text,
                "1 item left unchanged · Import review complete")
    }

    function test_incomplete_import_preview_cannot_be_confirmed() {
        useConnectedFixture()
        fixtureModel.importSnapshot.pageComplete = false
        wait(0)
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerImportReviewButton_opaque-batch-1"))
        wait(0)
        compare(findChild(page, "trackerImportPageIncomplete").visible, true)
        compare(findChild(page, "trackerConfirmImportButton").enabled, false)
        compare(fixtureModel.importConfirmCalls, 0)
    }

    function test_bulk_import_choice_previews_selected_titles_and_leaves_exceptions_visible() {
        useConnectedFixture()
        fixtureModel.importReviews = [{
            batchId: "opaque-batch-1",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            reviewCount: 4,
            decisionCount: 4,
            awaitingApplyCount: 0,
            unresolvedCount: 0,
            pageComplete: true,
            confirmed: false
        }]
        fixtureModel.importSnapshot = {
            accepted: true,
            code: "ready",
            batchId: "opaque-batch-1",
            providerKey: "trakt",
            providerName: "Trakt",
            initialImport: true,
            pageComplete: true,
            confirmed: false,
            items: [{
                reviewItemId: "bulk-a",
                title: "Safe new title A",
                classification: "new_progress",
                state: "review_required",
                matched: true,
                providerProgress: 4,
                providerCompleted: false,
                hasLocalAtPreview: false,
                nativeHistoryProtected: false,
                allowedChoices: ["use_provider_progress", "leave_unresolved"]
            }, {
                reviewItemId: "bulk-b",
                title: "Safe new title B",
                classification: "remote_advance",
                state: "review_required",
                matched: true,
                providerProgress: 7,
                providerCompleted: false,
                hasLocalAtPreview: true,
                localProgress: 3,
                localCompleted: false,
                nativeHistoryProtected: false,
                allowedChoices: ["use_provider_progress", "leave_unresolved"]
            }, {
                reviewItemId: "bulk-protected",
                title: "Protected title",
                classification: "disagreement",
                state: "review_required",
                matched: true,
                providerProgress: 12,
                providerCompleted: false,
                hasLocalAtPreview: true,
                localProgress: 8,
                localCompleted: false,
                nativeHistoryProtected: true,
                allowedChoices: ["keep_colosseum"]
            }, {
                reviewItemId: "bulk-unmatched",
                title: "Unmatched title",
                classification: "needs_matching",
                state: "review_required",
                matched: false,
                providerProgress: 2,
                providerCompleted: false,
                hasLocalAtPreview: false,
                nativeHistoryProtected: false,
                allowedChoices: ["leave_unmatched"]
            }]
        }
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerImportReviewButton_opaque-batch-1"))
        wait(0)

        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-a"))
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-b"))
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-protected"))
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-unmatched"))
        wait(0)
        verify(findChild(page, "trackerImportBulkExceptionNotice").visible)
        compare(findChild(page, "trackerImportBulkReviewButton").enabled, false)
        verify(findChild(page, "trackerImportBulkReviewDialog") !== null)
        compare(findChild(page, "trackerImportBulkReviewDialog").visible, false)
        compare(page.bulkImportReviewOpen, false)

        mouseClick(findChild(page, "trackerImportBulkClearButton"))
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-a"))
        mouseClick(findChild(page, "trackerImportBulkSelect_bulk-b"))
        wait(0)
        tryCompare(findChild(page, "trackerImportBulkSelectionCount"), "text",
                   "2 items selected", 1000)
        mouseClick(findChild(page, "trackerImportBulkReviewButton"))
        tryCompare(page, "bulkImportReviewOpen", true, 1000)

        verify(findChild(page, "trackerImportBulkReviewDialog") !== null)
        compare(findChild(page, "trackerImportBulkReviewTitles").text,
                "Safe new title A · Safe new title B")
        verify(findChild(page, "trackerImportBulkChoice_use_provider_progress") !== null)
        compare(fixtureModel.importChoiceCalls, 0)
        var useProgress = findChild(page, "trackerImportBulkChoice_use_provider_progress")
        var leaveUnresolved = findChild(page, "trackerImportBulkChoice_leave_unresolved")
        tryCompare(useProgress, "visible", true, 1000)
        tryCompare(leaveUnresolved, "visible", true, 1000)
        compare(useProgress.text, "Use tracker progress for 2 selected items")
        compare(useProgress.enabled, true)
        tryVerify(function() {
            return useProgress.width > 0 && useProgress.height > 0
                    && leaveUnresolved.width > 0 && leaveUnresolved.height > 0
                    && (useProgress.y + useProgress.height <= leaveUnresolved.y
                        || leaveUnresolved.y + leaveUnresolved.height <= useProgress.y)
        }, 1000)
        mouseClick(useProgress, useProgress.width / 2, useProgress.height / 2)
        tryCompare(fixtureModel, "importChoiceCalls", 2, 1000)

        tryCompare(fixtureModel, "lastBulkChoice", "use_provider_progress", 1000)
        tryCompare(page, "bulkImportReviewOpen", false, 1000)
        tryVerify(function() {
            return fixtureModel.importSnapshot.items[0].state === "waiting_to_apply"
                    && fixtureModel.importSnapshot.items[1].state === "waiting_to_apply"
        }, 1000)
        compare(page.selectedBulkImportItemIds.length, 0)
        compare(fixtureModel.importSnapshot.items[0].state, "waiting_to_apply")
        compare(fixtureModel.importSnapshot.items[1].state, "waiting_to_apply")
        compare(fixtureModel.importSnapshot.items[2].state, "review_required")
        compare(fixtureModel.importSnapshot.items[3].state, "review_required")
        compare(findChild(page, "trackerConfirmImportButton").enabled, false)
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function test_bulk_import_review_revalidates_its_opening_revision_before_saving() {
        useBulkImportFixture()
        openFixtureBulkImportReview()

        compare(findChild(page, "trackerImportBulkReviewTitles").text,
                "Safe new title A · Safe new title B")
        var openedRevision = page.bulkImportReviewRevision
        compare(openedRevision, fixtureModel.revision)

        fixtureModel.revision++
        compare(page.confirmBulkImportChoice("use_provider_progress"), false)
        compare(page.bulkImportReviewOpen, false)
        compare(page.selectedBulkImportItemIds.length, 0)
        compare(fixtureModel.importChoiceCalls, 0)
        compare(fixtureModel.lastImportChoiceExpectedRevision, -1)
        compare(fixtureModel.importSnapshot.items[0].state, "review_required")
        compare(page.importReviewRevision(), fixtureModel.revision)
    }

    function test_escape_closes_bulk_review_then_import_review_before_dossier() {
        useBulkImportFixture()
        openFixtureBulkImportReview()
        compare(page.dossierOpen, true)
        compare(page.importReviewOpen, true)

        compare(page.requestEscape(), true)
        compare(page.bulkImportReviewOpen, false)
        compare(page.importReviewOpen, true)
        compare(page.dossierOpen, true)
        compare(page.requestEscape(), true)
        compare(page.importReviewOpen, false)
        compare(page.dossierOpen, true)
        compare(backSpy.count, 0)
    }

    function test_disconnect_reviews_pending_work_and_never_removes_native_state() {
        useConnectedFixture()
        var trackerCard = findChild(page, "trackerCard_trakt")
        tryCompare(trackerCard, "visible", true, 1000)
        mouseClick(trackerCard)
        tryCompare(page, "selectedProviderKey", "trakt", 1000)

        var disconnectButton = findChild(page, "trackerDisconnectButton")
        verify(disconnectButton !== null)
        tryCompare(disconnectButton, "visible", true, 1000)
        tryCompare(disconnectButton, "enabled", true, 1000)
        revealDossierItem(disconnectButton)
        mouseClick(disconnectButton)
        tryCompare(page, "disconnectReviewOpen", true, 1000)

        verify(findChild(page, "trackerDisconnectDialog") !== null)
        tryCompare(findChild(page, "trackerDisconnectUnknownWarning"), "visible", true, 1000)
        verify(findChild(page, "trackerDisconnectKeepPaused") !== null)
        verify(findChild(page, "trackerDisconnectDiscardKnownUnsent") !== null)
        verify(findChild(page, "trackerDisconnectCancel") !== null)

        mouseClick(findChild(page, "trackerDisconnectKeepPaused"))
        tryCompare(page, "disconnectReviewOpen", false, 1000)
        compare(fixtureModel.disconnectCalls, 1)
        compare(fixtureModel.lastDisconnectProvider, "trakt")
        compare(fixtureModel.lastDisconnectChoice, "keep_paused")
        compare(fixtureModel.importReviews.length, 1)
        compare(fixtureModel.providerNetworkCalls, 0)
        compare(page.selectedDossier.connected, false)
        page.closeDossier()
        tryCompare(page, "selectedProviderKey", "", 1000)
        verify(page.openDossier("trakt"))
        tryCompare(page, "selectedProviderKey", "trakt", 1000)
        compare(findChild(page, "trackerAttentionRegion").visible, true)
        tryCompare(findChild(page, "trackerDeliveryQueueSection"), "visible", true, 1000)
        compare(findChild(page, "trackerDisconnectedDeliveryNotice").visible, true)
        compare(findChild(page, "trackerDeliveryState").text, "Checking delivery")
    }

    function test_disconnect_cancel_leaves_tracker_connected() {
        openConnectedDossier()
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        mouseClick(findChild(page, "trackerDisconnectCancel"))
        wait(0)
        compare(fixtureModel.disconnectCalls, 0)
        compare(page.selectedDossier.connected, true)
        compare(page.disconnectReviewOpen, false)
    }

    function test_disconnect_modal_traps_keyboard_focus_and_returns_it() {
        openConnectedDossier()
        var disconnectButton = findChild(page, "trackerDisconnectButton")
        var keepPaused = findChild(page, "trackerDisconnectKeepPaused")
        var discard = findChild(page, "trackerDisconnectDiscardKnownUnsent")
        var cancel = findChild(page, "trackerDisconnectCancel")
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        wait(0)
        activateTestWindow()
        tryCompare(keepPaused, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(discard, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(cancel, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(keepPaused, "activeFocus", true, 1000)
        keyClick(Qt.Key_Escape)
        wait(0)
        compare(page.disconnectReviewOpen, false)
        tryCompare(disconnectButton, "activeFocus", true, 1000)
    }

    function test_disconnect_partial_cleanup_stays_paused_and_can_retry() {
        openConnectedDossier()
        fixtureModel.disconnectCleanupFailsOnce = true
        var disconnectButton = findChild(page, "trackerDisconnectButton")
        tryCompare(disconnectButton, "visible", true, 1000)
        tryCompare(disconnectButton, "enabled", true, 1000)
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        mouseClick(findChild(page, "trackerDisconnectDiscardKnownUnsent"))
        tryCompare(fixtureModel, "disconnectCalls", 1, 1000)

        compare(fixtureModel.lastActionResult.code, "disconnect_cleanup_pending")
        compare(page.selectedDossier.connected, false)
        compare(page.selectedDossier.knownUnsentCount, 1)
        verify(findChild(page, "trackerDisconnectDialogNotice").text.indexOf("disconnected") >= 0)
        verify(findChild(page, "trackerDisconnectUnknownWarning").visible)
        compare(findChild(page, "trackerDisconnectButton").visible, true)
        compare(findChild(page, "trackerDisconnectButton").enabled, true)
        compare(findChild(page, "trackerDisconnectButton").text,
                "Finish removing known-unsent updates")

        mouseClick(findChild(page, "trackerDisconnectCancel"))
        wait(0)
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        compare(findChild(page, "trackerDisconnectTitle").text,
                "Finish removing known-unsent updates")
        compare(findChild(page, "trackerDisconnectKeepPaused").text,
                "Leave remaining work paused")
        compare(findChild(page, "trackerDisconnectDiscardKnownUnsent").text,
                "Retry removing known-unsent updates")
        verify(findChild(page, "trackerDisconnectUnknownWarning").visible)
        mouseClick(findChild(page, "trackerDisconnectDiscardKnownUnsent"))
        wait(0)

        compare(fixtureModel.disconnectCalls, 2)
        compare(page.disconnectReviewOpen, false)
        compare(page.selectedDossier.knownUnsentCount, 0)
        compare(page.selectedDossier.cleanupEnabled, false)
        compare(fixtureModel.providerNetworkCalls, 0)
        compare(fixtureModel.importReviews.length, 1)
        compare(findChild(page, "trackerDeliveryState").text, "Checking delivery")
    }

    function test_remove_imported_history_requires_review_and_preserves_colosseum_state() {
        useConnectedFixture()
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        var removeButton = findChild(page, "trackerRemoveImportedData")
        verify(removeButton !== null)
        compare(removeButton.visible, true)
        mouseClick(removeButton)
        wait(0)

        verify(findChild(page, "trackerRemoveImportedDialog") !== null)
        compare(findChild(page, "trackerRemoveImportedCount").text.indexOf("2 tracker-imported History items"), 0)
        var removalPreview = findChild(page, "trackerRemoveImportedPreview")
        verify(removalPreview !== null)
        compare(removalPreview.count, 3)
        compare(findChild(page, "trackerRemoveImportedPreviewTitle").text, "Frieren")
        verify(JSON.stringify(page.selectedDossier.importedDataRemovalPreview).indexOf(
                   "remoteAccount") < 0)
        verify(JSON.stringify(page.selectedDossier.importedDataRemovalPreview).indexOf(
                   "remote-item") < 0)
        verify(findChild(page, "trackerRemoveImportedProtection").text.indexOf("Native History, Activity, statistics, local Progress") >= 0)
        compare(fixtureModel.removeImportedCalls, 0)
        var cancel = findChild(page, "trackerRemoveImportedCancel")
        var confirm = findChild(page, "trackerRemoveImportedConfirm")
        cancel.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(cancel, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(removalPreview, "activeFocus", true, 1000)
        keyClick(Qt.Key_End)
        compare(removalPreview.currentIndex, removalPreview.count - 1)
        keyClick(Qt.Key_Tab)
        tryCompare(confirm, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(cancel, "activeFocus", true, 1000)
        keyClick(Qt.Key_Escape)
        wait(0)
        compare(page.importedDataReviewOpen, false)
        compare(fixtureModel.removeImportedCalls, 0)
        compare(page.selectedDossier.importedHistoryCount, 2)

        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedCancel"))
        wait(0)
        compare(fixtureModel.removeImportedCalls, 0)
        compare(page.selectedDossier.importedHistoryCount, 2)

        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedConfirm"))
        wait(0)
        compare(fixtureModel.removeImportedCalls, 1)
        compare(page.selectedDossier.importedHistoryCount, 0)
        compare(page.selectedDossier.importedProgressCount, 0)
        compare(findChild(page, "trackerRemoveImportedData").visible, false)
        compare(fixtureModel.providerNetworkCalls, 0)
        compare(fixtureModel.importReviews.length, 1)
        verify(findChild(page, "trackerDisconnectNotice").text.indexOf("Native History") >= 0)
    }

    function test_escape_closes_remove_imported_review_before_dossier() {
        useConnectedFixture()
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedData"))
        tryCompare(page, "importedDataReviewOpen", true, 1000)
        compare(page.dossierOpen, true)

        compare(page.requestEscape(), true)
        compare(page.importedDataReviewOpen, false)
        compare(page.dossierOpen, true)
        compare(fixtureModel.removeImportedCalls, 0)
        compare(page.selectedDossier.importedHistoryCount, 2)
        compare(page.requestEscape(), true)
        compare(page.dossierOpen, false)
        compare(backSpy.count, 0)
    }

    function test_disconnect_review_closes_when_account_changes() {
        openConnectedDossier()
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        var reviewedRevision = page.disconnectReviewRevision

        var nextDossiers = Object.assign({}, fixtureModel.dossiers)
        nextDossiers.trakt = Object.assign({}, nextDossiers.trakt, {
            accountLabel: "A different tracker account"
        })
        fixtureModel.dossiers = nextDossiers
        fixtureModel.revision++
        fixtureModel.modelChanged()
        wait(0)

        compare(page.disconnectReviewOpen, false)
        compare(fixtureModel.disconnectCalls, 0)
        verify(fixtureModel.revision > reviewedRevision)
        verify(page.actionNotice.indexOf("changed while the review was open") >= 0)

        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        compare(page.disconnectReviewRevision, fixtureModel.revision)
        mouseClick(findChild(page, "trackerDisconnectCancel"))
        wait(0)
        compare(fixtureModel.disconnectCalls, 0)
    }

    function test_disconnect_review_closes_when_profile_model_changes_at_same_revision() {
        openConnectedDossier()
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        compare(page.disconnectReviewRevision, otherProfileModel.revision)

        modelUnderTest = otherProfileModel
        wait(0)

        compare(page.disconnectReviewOpen, false)
        compare(fixtureModel.disconnectCalls, 0)
        compare(page.selectedDossier.accountLabel, "Connected account")
        verify(page.actionNotice.indexOf("active Colosseum profile changed") >= 0)
    }

    function test_disconnect_confirmation_rejects_unannounced_revision_change() {
        openConnectedDossier()
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        fixtureModel.revision++

        mouseClick(findChild(page, "trackerDisconnectKeepPaused"))
        wait(0)

        compare(fixtureModel.disconnectCalls, 0)
        compare(page.disconnectReviewOpen, false)
        verify(page.actionNotice.indexOf("changed while the review was open") >= 0)
    }

    function test_remove_imported_review_closes_when_reviewed_set_changes() {
        useConnectedFixture()
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        verify(page.importedDataReviewOpen)
        var reviewedRevision = page.importedDataReviewRevision

        var nextDossiers = Object.assign({}, fixtureModel.dossiers)
        nextDossiers.trakt = Object.assign({}, nextDossiers.trakt, {
            importedHistoryCount: 1,
            importedProgressCount: 0,
            importedDataRemovalPreview: [{
                title: "Changed entry",
                dataKind: "History",
                consequence: "Only this tracker account's History evidence is removed."
            }]
        })
        fixtureModel.dossiers = nextDossiers
        fixtureModel.revision++
        fixtureModel.modelChanged()
        wait(0)

        compare(page.importedDataReviewOpen, false)
        compare(fixtureModel.removeImportedCalls, 0)
        verify(fixtureModel.revision > reviewedRevision)
        verify(page.actionNotice.indexOf("changed while the review was open") >= 0)

        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        verify(page.importedDataReviewOpen)
        compare(findChild(page, "trackerRemoveImportedCount").text.indexOf(
                    "1 tracker-imported History item"), 0)
        mouseClick(findChild(page, "trackerRemoveImportedCancel"))
        wait(0)
        compare(fixtureModel.removeImportedCalls, 0)
    }

    function test_remove_imported_review_closes_when_profile_model_changes_at_same_revision() {
        useConnectedFixture()
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        verify(page.importedDataReviewOpen)
        compare(page.importedDataReviewRevision, otherProfileModel.revision)

        modelUnderTest = otherProfileModel
        wait(0)

        compare(page.importedDataReviewOpen, false)
        compare(fixtureModel.removeImportedCalls, 0)
        compare(page.selectedDossier.importedDataRemovalPreview[0].title,
                "Other profile entry")
        verify(page.actionNotice.indexOf("active Colosseum profile changed") >= 0)
    }

    function test_remove_imported_confirmation_rejects_unannounced_revision_change() {
        useConnectedFixture()
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        verify(page.importedDataReviewOpen)
        fixtureModel.revision++

        mouseClick(findChild(page, "trackerRemoveImportedConfirm"))
        wait(0)

        compare(fixtureModel.removeImportedCalls, 0)
        compare(page.importedDataReviewOpen, false)
        verify(page.actionNotice.indexOf("changed while the review was open") >= 0)
    }

    function test_removal_preview_is_keyboard_scrollable_before_confirmation() {
        useConnectedFixture()
        var preview = []
        for (var i = 0; i < 10; ++i) {
            preview.push({
                title: "Imported entry " + i,
                dataKind: "History",
                consequence: "Only this tracker account's History evidence is removed."
            })
        }
        var nextDossiers = Object.assign({}, fixtureModel.dossiers)
        nextDossiers.trakt = Object.assign({}, nextDossiers.trakt, {
            importedHistoryCount: 10,
            importedProgressCount: 0,
            importedDataRemovalPreview: preview,
            removeImportedEnabled: true
        })
        fixtureModel.dossiers = nextDossiers
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)

        tryCompare(page, "importedDataReviewOpen", true, 1000)
        activateTestWindow()

        var list = findChild(page, "trackerRemoveImportedPreview")
        var confirm = findChild(page, "trackerRemoveImportedConfirm")
        var cancel = findChild(page, "trackerRemoveImportedCancel")
        verify(list !== null)
        verify(cancel !== null)
        compare(list.count, 10)
        cancel.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(cancel, "activeFocus", true, 1000)
        keyClick(Qt.Key_Tab)
        tryCompare(list, "activeFocus", true, 1000)
        keyClick(Qt.Key_End)
        compare(list.currentIndex, 9)
        verify(list.contentY > 0)
        verify(findChild(page, "trackerRemoveImportedPreviewTitle_9") !== null)
        keyClick(Qt.Key_Tab)
        tryCompare(confirm, "activeFocus", true, 1000)
        compare(fixtureModel.removeImportedCalls, 0)
        mouseClick(findChild(page, "trackerRemoveImportedCancel"))
        wait(0)
        compare(fixtureModel.removeImportedCalls, 0)
    }

    function test_remove_imported_data_works_when_only_tracker_progress_exists() {
        useConnectedFixture()
        var nextDossiers = Object.assign({}, fixtureModel.dossiers)
        nextDossiers.trakt = Object.assign({}, nextDossiers.trakt, {
            importedHistoryCount: 0,
            importedProgressCount: 2,
            importedDataRemovalPreview: [
                { title: "Progress A", dataKind: "Progress",
                  consequence: "This tracker-imported Progress will be removed." },
                { title: "Progress B", dataKind: "Progress",
                  consequence: "This tracker-imported Progress will be removed." }
            ],
            removeImportedEnabled: true
        })
        fixtureModel.dossiers = nextDossiers
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)

        compare(page.selectedDossier.importedHistoryCount, 0)
        compare(page.selectedDossier.importedProgressCount, 2)
        var removeButton = findChild(page, "trackerRemoveImportedData")
        verify(removeButton !== null)
        compare(removeButton.visible, true)
        mouseClick(removeButton)
        wait(0)
        verify(page.importedDataReviewOpen)
        verify(findChild(page, "trackerRemoveImportedCount").text.indexOf(
                   "2 tracker-imported Progress items") >= 0)
        compare(findChild(page, "trackerRemoveImportedPreview").count, 2)
        mouseClick(findChild(page, "trackerRemoveImportedConfirm"))
        wait(0)

        compare(fixtureModel.removeImportedCalls, 1)
        compare(page.selectedDossier.importedHistoryCount, 0)
        compare(page.selectedDossier.importedProgressCount, 0)
        compare(findChild(page, "trackerRemoveImportedData").visible, false)
        compare(fixtureModel.providerNetworkCalls, 0)
    }

    function test_remove_imported_data_requires_fresh_preview_after_pending_source_change() {
        useConnectedFixture()
        fixtureModel.removeImportedPendingScenario = true
        mouseClick(findChild(page, "trackerCard_trakt"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        mouseClick(findChild(page, "trackerRemoveImportedConfirm"))
        wait(0)

        verify(page.importedDataReviewOpen)
        compare(findChild(page, "trackerRemoveImportedConfirm").enabled, false)
        verify(findChild(page, "trackerRemoveImportedNotice").text.indexOf(
                   "stay open until Colosseum confirms the result") >= 0)
        compare(page.actionNotice, "")

        var changedDossiers = Object.assign({}, fixtureModel.dossiers)
        changedDossiers.trakt = Object.assign({}, fixtureModel.dossiers.trakt, {
            accountLabel: "Replacement account",
            importedHistoryCount: 3,
            importedProgressCount: 1,
            importedDataRemovalPreview: [
                { title: "Replacement import A", dataKind: "History",
                  consequence: "Only this tracker account's History evidence is removed." },
                { title: "Replacement import B", dataKind: "History",
                  consequence: "Only this tracker account's History evidence is removed." },
                { title: "Replacement import C", dataKind: "History",
                  consequence: "Only this tracker account's History evidence is removed." },
                { title: "Replacement progress", dataKind: "Progress",
                  consequence: "This tracker-imported Progress will be removed." }
            ]
        })
        fixtureModel.dossiers = changedDossiers
        fixtureModel.revision++
        fixtureModel.modelChanged()
        wait(0)

        // A confirmed native operation may finish after the visible account
        // and imported set change. The pending dialog cannot confirm again.
        compare(page.importedDataReviewOpen, true)
        compare(findChild(page, "trackerRemoveImportedConfirm").enabled, false)
        compare(page.selectedDossier.accountLabel, "Replacement account")

        fixtureModel.finishImportedRemoval("progress_removed_history_removal_failed")
        tryCompare(page, "importedRemovalAwaitingResult", false, 1000)
        compare(page.importedDataReviewOpen, false)
        compare(page.selectedDossier.accountLabel, "Replacement account")
        compare(page.selectedDossier.importedHistoryCount, 3)
        compare(page.selectedDossier.importedProgressCount, 0)
        verify(page.actionNotice.indexOf(
                   "Progress was removed, but its History evidence remains") >= 0)
        verify(page.actionNotice.indexOf("Reopen Remove imported data") >= 0)

        mouseClick(findChild(page, "trackerRemoveImportedData"))
        wait(0)
        compare(page.importedDataReviewOpen, true)
        compare(page.importedDataReviewAccountLabel, "Replacement account")
        compare(findChild(page, "trackerRemoveImportedCount").text.indexOf(
                    "3 tracker-imported History items") >= 0, true)
        compare(findChild(page, "trackerRemoveImportedPreview").count, 3)
        tryVerify(function() {
            return findChild(page, "trackerRemoveImportedPreviewTitle") !== null
        })
        compare(findChild(page, "trackerRemoveImportedPreviewTitle").text,
                "Replacement import A")
        compare(fixtureModel.removeImportedCalls, 1)

        mouseClick(findChild(page, "trackerRemoveImportedConfirm"))
        wait(0)
        compare(fixtureModel.removeImportedCalls, 2)
        compare(page.importedDataReviewOpen, false)
        verify(page.actionNotice.indexOf("were removed") >= 0)
    }

    function test_disconnect_discard_choice_is_limited_to_known_unsent_work() {
        openConnectedDossier()
        verify(page.openDisconnectReview())
        tryCompare(page, "disconnectReviewOpen", true, 1000)
        compare(findChild(page, "trackerDisconnectUnknownWarning").visible, true)
        compare(findChild(page, "trackerDisconnectPendingCounts").text,
                "Pending: 2 · Known unsent: 1 · Unknown outcome: 1")
        verify(findChild(page, "trackerDisconnectRemoteAccessNotice").text.indexOf(
                   "ask the tracker to revoke this app's access") >= 0)
        verify(findChild(page, "trackerDisconnectRemoteAccessNotice").text.indexOf(
                   "best-effort") >= 0)
        mouseClick(findChild(page, "trackerDisconnectDiscardKnownUnsent"))
        wait(0)
        compare(fixtureModel.disconnectCalls, 1)
        compare(fixtureModel.lastDisconnectChoice, "discard_known_unsent")
        compare(fixtureModel.importReviews.length, 1)
        compare(fixtureModel.providerNetworkCalls, 0)
        compare(page.selectedDossier.connected, false)
    }

    function test_main_sync_deep_link_stays_outside_tracker_registry() {
        compare(TrackerSyncCenter.connectedTrackers.length, 0)
        var stremioIcon = findChild(page, "trackerStremioIcon")
        verify(stremioIcon !== null)
        verify(stremioIcon.source.toString().indexOf("stremio-official.svg") >= 0)
        verify(findChild(page, "trackerMainSyncButton").height <= 40)
        mouseClick(findChild(page, "trackerMainSyncButton"))
        compare(mainSyncSpy.count, 1)
        compare(TrackerSyncCenter.connectedTrackers.length, 0)
    }

    function test_escape_request_closes_dossier_before_leaving_page() {
        var catalogueButton = findChild(page, "trackerCatalogue_simkl")
        revealCatalogueCards()
        mouseClick(catalogueButton)
        wait(0)
        compare(page.dossierOpen, true)
        compare(page.requestEscape(), true)
        compare(page.dossierOpen, false)
        tryCompare(catalogueButton, "activeFocus", true, 1000)
        compare(backSpy.count, 0)
        compare(page.requestEscape(), false)
        compare(backSpy.count, 1)
    }

    function test_directional_navigation_moves_focus_between_catalogue_cards() {
        revealCatalogueCards()
        var simkl = findChild(page, "trackerCatalogue_simkl")
        var mal = findChild(page, "trackerCatalogue_mal")
        verify(simkl !== null)
        verify(mal !== null)
        simkl.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(simkl, "activeFocus", true, 1000)
        keyClick(Qt.Key_Right)
        tryCompare(mal, "activeFocus", true, 1000)
    }

    function test_layout_and_motion_adapt_without_reordering() {
        verify(page.catalogueColumns >= 2)
        page.reducedMotion = true
        compare(page.motionEnabled, false)
        testWindow.width = 680
        wait(0)
        compare(page.catalogueColumns, 1)
        verify(findChild(page, "trackerMainSyncButton") !== null)
    }
}
