// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Qt WebEngine download APIs follow Qt Quick Nano Browser; see NanoBrowser-LICENSE.txt.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import ".."
import "../account" as Account

Item {
    id: root
    // Exactly one direct native listener per profile, never per view/window.
    required property var profile
    property var accountStore: null
    property var policy: null
    property bool privateBrowsing: false
    property bool active: profile !== null
    property bool retired: false
    property bool idleProfileDetached: false
    property bool panelOpen: false
    property Item panelSurface: null
    // Native handles are internal adapter state. Presentation consumes rows only.
    property var items: []
    property var draining: []
    property var rows: []
    property var seenNativeIds: ({})
    property int profileGeneration: 0
    property int requestSerial: 0
    property int decisionSerial: 0
    property string ownerBinding: "qt-download-owner-" + Date.now() + "-" + Math.random().toString(36).slice(2)
    property string destinationPolicy: "autoSave"
    // Pinned Qt Quick view() dereferences a raw adapterClient even for an
    // originless request. page() supplies no Quick alternative. Never evaluate
    // either getter here. A future qualified adapter may supply an immutable
    // envelope; URL/order/current selection are never acceptable substitutes.
    property var sourceResolver: null
    property bool componentReady: false
    property var nativeSaveIntents: ({})
    property string nativeSaveBlockedReason: ""
    property bool pageSaveIngressExclusive: false
    property var pageSaveIntent: null
    property var rejectedPageRequest: null
    property string rejectedPageReason: ""
    property var nativeSaveOutcome: ({})
    signal nativeSaveStateChanged()
    readonly property string sourceBindingCapability: sourceResolver ? "provided" : "blocked-qt-quick-source-lifetime"
    readonly property var history: !privateBrowsing && accountStore ? accountStore.browserDownloadHistory || [] : []
    property bool hasTransferDependencies: false
    property bool retirementPending: false
    signal message(string text)
    signal downloadOffered(string downloadId, string decisionToken, var row)
    signal downloadDependenciesChanged()
    Theme { id: theme }

    function openPanel() { panelOpen = true }
    function cancelActive() { retire("account-retired") }
    function registerOwner() {
        if (policy && accountStore && typeof policy.registerDownloadOwner === "function")
            policy.registerDownloadOwner(root, accountStore)
    }
    function rebindAccountScope() {
        // The deck calls this only after its quiescence/current-profile check.
        // Never regrant existing settled capabilities to a different account.
        if (!profile || !active || items.length || !policy || !accountStore
                || profile.accountRoot !== policy.storageRoot) return false
        if (!policy.registerDownloadOwner(root, accountStore)) return false
        retired = false
        publish()
        return true
    }
    function sourceForRequest(request) {
        if (request.isSavePageDownload && pageSaveIntent)
            return JSON.parse(JSON.stringify(pageSaveIntent.source))
        if (!sourceResolver) return {}
        var captured = sourceResolver(request)
        return captured ? JSON.parse(JSON.stringify(captured)) : {}
    }
    function createEntry(request, source) {
        var accountScope = policy && typeof policy.downloadOwnerScope === "function"
            ? policy.downloadOwnerScope(root) : {}
        if (policy && typeof policy.downloadOwnerScope === "function" && accountScope.accountGeneration === undefined) {
            request.cancel()
            message(qsTr("The download belongs to a retired account scope."))
            return null
        }
        var nativeId = request.id === undefined ? "unavailable-" + (++requestSerial) : String(request.id)
        var profileId = ownerBinding + ":" + profileGeneration
        var downloadId = profileId + ":" + nativeId
        var admission = policy && typeof policy.admitDownloadDestination === "function"
            ? policy.admitDownloadDestination(request.downloadDirectory, request.downloadFileName) : {}
        if (policy && admission.status !== "admitted") {
            request.cancel()
            message(qsTr("The download destination is unavailable or unsafe."))
            return null
        }
        var isPrivate = privateBrowsing || !!profile.offTheRecord
        var sessionId = String(source.spaceId || source.sessionId || "")
        var visitToken = String(source.visitToken || "")
        var entry = downloadEntry.createObject(root, {
            request: request, source: source, downloadId: downloadId,
            profileId: profileId, nativeId: nativeId, admission: admission, accountScope: accountScope,
            decisionToken: downloadId + ":decision:" + (++decisionSerial),
            privateDownload: isPrivate, metadataStore: accountStore,
            transferKind: request.isSavePageDownload ? "pageSerialization" : "resource",
            owningProfile: profile, sessionId: sessionId, visitToken: visitToken,
            name: request.downloadFileName, directory: request.downloadDirectory,
            received: request.receivedBytes, total: request.totalBytes, state: request.state,
            paused: request.isPaused, finished: request.isFinished, sourceUrl: String(request.url)
        })
        return entry
    }
    function receive(request) {
        if (!request) return
        var key = request.id === undefined ? "" : String(request.id)
        if ((key.length && seenNativeIds[key]) || items.some(function(item) { return item.request === request })) return
        if (!active || retired) { request.cancel(); return }
        if (policy && typeof policy.requestBelongsToProfile === "function"
                && !policy.requestBelongsToProfile(request, profile)) { request.cancel(); return }
        // Rejected offers are one-shot too. A late replay may not consume a
        // newer page intent after the original rejection has settled.
        if (key.length) seenNativeIds[key] = "offered"
        if (request.isSavePageDownload) {
            // The pinned engine also sets this flag on an ordinary resource
            // that consumes pending SavePageInfo. Only the ORIGINAL known
            // format identifies ChooseSavePath's native HTML save package.
            if ([0, 1, 2].indexOf(request.savePageFormat) < 0) {
                request.cancel()
                nativeSaveBlockedReason = qsTr("This request has no proven HTML page-save package identity. Non-text page Save is unsupported.")
                message(nativeSaveBlockedReason)
                // Do not bind this request or clear a different pending package.
                return
            }
            if (!pageSaveIngressExclusive || !pageSaveIntent || pageSaveIntent.downloadId
                    || pageSaveIntent.invalidated || !pageSaveSourceCurrent()) {
                rejectPageOffer(request, "source-invalidated")
                message(qsTr("This page-save request has no current app-owned Save operation."))
                return
            }
            if (pageSaveIntent.format === 0 || pageSaveIntent.format === 1 || pageSaveIntent.format === 2)
                request.savePageFormat = pageSaveIntent.format
        }
        // Capture only supported, independently established source facts. This
        // path intentionally does not recover a source from the unsafe getter.
        var source = sourceForRequest(request)
        var entry = createEntry(request, source)
        if (!entry) {
            if (request.isSavePageDownload) rejectPageOffer(request, "destination-rejected")
            return
        }
        if (key.length) seenNativeIds[key] = entry.downloadId
        items = items.concat([entry])
        if (request.isSavePageDownload) {
            pageSaveIntent.downloadId = entry.downloadId
            nativeSaveStateChanged()
        }
        if (destinationPolicy === "autoSave") entry.acceptDecision(entry.decisionToken, {})
        else downloadOffered(entry.downloadId, entry.decisionToken, entry.snapshot())
        entry.scheduleUpdate()
        publish()
        panelOpen = true
    }
    function rejectPageOffer(request, reason) {
        var ownsSlot = pageSaveIngressExclusive && pageSaveIntent && !pageSaveIntent.downloadId
        if (ownsSlot) {
            rejectedPageRequest = request
            rejectedPageReason = reason
            if (reason === "source-invalidated") pageSaveIntent.invalidated = true
        }
        if (!request.isFinished) request.cancel()
        if (ownsSlot) Qt.callLater(settleRejectedPageOffer)
    }
    function settleRejectedPageOffer() {
        var request = rejectedPageRequest
        if (!pageSaveIntent || !request || !request.isFinished) return
        nativeSaveOutcome = {status: pageSaveIntent.invalidated || rejectedPageReason === "source-invalidated" ? "invalidated"
            : rejectedPageReason === "destination-rejected" ? "rejected"
            : request.state === WebEngineDownloadRequest.DownloadCancelled ? "cancelled" : "failed",
            reason: rejectedPageReason, operationId: pageSaveIntent.source.operationId,
            nativeState: request.state, nativeDrain: "wrapper-terminal-only"}
        rejectedPageRequest = null
        rejectedPageReason = ""
        pageSaveIntent = null
        nativeSaveStateChanged()
        publish()
    }
    function failNativeSaveDownload(id, reason) {
        if (!pageSaveIntent || pageSaveIntent.downloadId !== id) return
        pageSaveIntent.invalidated = true
        nativeSaveOutcome = {status: "failed", reason: reason, downloadId: id,
            operationId: pageSaveIntent.source.operationId, nativeDrain: "unconfirmed"}
        // Request loss is not a transfer-drain receipt. The explicit profile
        // teardown path can retire this unresolved document dependency.
        nativeSaveStateChanged()
    }
    function beginNativeSave(browser, intent) {
        nativeSaveBlockedReason = ""
        if (!active || retired || !browser || browser.profile !== profile || !intent) {
            nativeSaveBlockedReason = qsTr("The source page is no longer available.")
            return ""
        }
        if (intent.kind === "page") {
            if (!pageSaveIngressExclusive) {
                nativeSaveBlockedReason = qsTr("Page Save needs an exclusive app-owned Save entry point.")
                return ""
            }
            if (pageSaveIntent) {
                nativeSaveBlockedReason = qsTr("Finish or retire the current page Save before starting another.")
                return ""
            }
            var pageToken = ownerBinding + ":page-save:" + (++requestSerial)
            var source = Object.assign({}, intent.binding || {}, {
                sessionId: String(browser.accountSessionId || ""),
                visitToken: String(browser.accountVisitToken || ""),
                operationId: String(intent.operationId || ""),
                sourceStatus: "exclusive-app-page-save", frameProvenance: "main-document"})
            pageSaveIntent = {token: pageToken, browser: browser, profile: profile,
                navigationGeneration: browser.navigationGeneration, source: source,
                format: intent.format, downloadId: "", invalidated: false}
            nativeSaveOutcome = {status: "dispatched", operationId: source.operationId}
            nativeSaveStateChanged()
            publish()
            return pageToken
        }
        if (["link", "image", "media"].indexOf(intent.kind) < 0) return ""
        var token = ownerBinding + ":save-intent:" + (++requestSerial)
        var next = Object.assign({}, nativeSaveIntents)
        // Dispatch receipts only. These are never associated with a native row
        // by URL, event order, selection or the raw-backed view getter.
        next[token] = {kind: intent.kind, operationId: String(intent.operationId || ""),
            status: "dispatched-source-correlation-unavailable"}
        var keys = Object.keys(next)
        for (var i = 0; i < keys.length - 100; ++i) delete next[keys[i]]
        nativeSaveIntents = next
        return token
    }
    function cancelNativeSave(token, reason) {
        if (pageSaveIntent && pageSaveIntent.token === token) {
            invalidateNativeSave(pageSaveIntent.browser, reason)
            return true
        }
        if (!nativeSaveIntents[token]) return false
        var next = Object.assign({}, nativeSaveIntents)
        delete next[token]
        nativeSaveIntents = next
        return true
    }
    function pageSaveSourceCurrent() {
        var pending = pageSaveIntent
        if (pending && pending.browser && pending.browser.saveBinding) {
            var current = pending.browser.saveBinding
            for (var key of ["accountEpoch", "profileId", "spaceId", "pageGeneration", "documentGeneration"])
                if (String(pending.source[key]) !== String(current[key])) return false
        }
        return !!pending && !!pending.browser && !pending.invalidated
            && pending.profile === profile && pending.browser.profile === profile
            && pending.browser.navigationGeneration === pending.navigationGeneration
            && !pending.browser.rendererLost && !pending.browser.closingSession
    }
    function pendingPageSaveForView(browser) {
        return !!pageSaveIntent && pageSaveIntent.browser === browser
    }
    function invalidateNativeSave(browser, reason) {
        if (!pageSaveIntent || pageSaveIntent.browser !== browser) return false
        pageSaveIntent.invalidated = true
        nativeSaveOutcome = {status: "invalidated", reason: String(reason || "source-retired"),
            operationId: pageSaveIntent.source.operationId, nativeDrain: "unconfirmed"}
        var entry = findEntry(pageSaveIntent.downloadId)
        if (entry && entry.request && !entry.request.isFinished) entry.cancel()
        // Before native delivery there is no supported cancellation handle.
        // Retain the barrier until profile teardown; no timeout is a drain ACK.
        nativeSaveStateChanged()
        publish()
        return true
    }
    function settleNativeSave(entry) {
        if (!pageSaveIntent || pageSaveIntent.downloadId !== entry.downloadId || !entry.finished) return
        nativeSaveOutcome = {status: pageSaveIntent.invalidated ? "invalidated"
            : entry.completed ? "succeeded" : entry.state === WebEngineDownloadRequest.DownloadCancelled ? "cancelled" : "failed",
            operationId: pageSaveIntent.source.operationId, downloadId: entry.downloadId,
            nativeDrain: "wrapper-terminal-only"}
        pageSaveIntent = null
        nativeSaveStateChanged()
    }
    function findEntry(id) {
        for (var entry of items) if (entry.downloadId === id) return entry
        return null
    }
    function decideDownload(id, token, decision) {
        var entry = findEntry(id)
        if (!entry || retired || !active) return {status: "invalidated"}
        return entry.acceptDecision(token, decision || {})
    }
    function requestDownloadAction(id, operationId, action) {
        var entry = findEntry(id)
        if (!entry || retired || !active || !operationId) return {status: "invalidated"}
        return entry.performAction(operationId, action)
    }
    function retryMetadata(id) {
        var entry = findEntry(id)
        if (!entry || entry.metadataState !== "retryable" || retired) return {status: "invalidated"}
        entry.commitMetadata()
        publish()
        return {status: entry.metadataState}
    }
    function resolveDownloadFile(id) {
        var entry = findEntry(id)
        if (!entry || retired || !entry.completed
                || (!entry.detached && (!active || entry.owningProfile !== profile))) return {}
        return entry.fileSnapshot
    }
    // Dedicated completed-file action interface. It remains available for a
    // detached private row; native transfer actions correctly require a profile.
    function fileAction(id, historical, reveal) {
        if (!policy) return {status: "unsupported"}
        var owner = historical ? accountStore : root
        var result = reveal ? policy.revealDownloadedFile(owner, id) : policy.openDownloadedFile(owner, id)
        if (result.status !== "opened") message(qsTr("This downloaded file is no longer available to open."))
        return result
    }
    function publish() {
        rows = items.map(function(entry) { return entry.snapshot() })
        var dependent = !!pageSaveIntent || items.some(function(entry) { return entry.dependsOnProfile() })
        var pending = draining.some(function(entry) { return entry.dependsOnProfile() })
        var changed = hasTransferDependencies !== dependent || retirementPending !== pending
        hasTransferDependencies = dependent
        retirementPending = pending
        if (changed) downloadDependenciesChanged()
    }
    function pruneFinished() {
        var finished = items.filter(function(entry) { return !entry.dependsOnProfile() && entry.metadataState !== "retryable" })
        var remove = finished.slice(0, Math.max(0, finished.length - 100))
        if (!remove.length) return
        items = items.filter(function(entry) { return remove.indexOf(entry) < 0 })
        for (var entry of remove) { entry.request = null; entry.destroy() }
    }
    function retire(reason) {
        retired = !!reason
        idleProfileDetached = false
        nativeSaveIntents = ({})
        panelOpen = false
        var previous = items
        items = []
        for (var entry of previous) {
            entry.invalidated = true
            entry.releaseAuthorization()
            if (entry.request && !entry.request.isFinished) entry.request.cancel()
            // Keep QObject references/observers through profile destruction.
            // Cancelled/isFinished describe Qt's wrapper, not a physical-file
            // cleanup or native-drain acknowledgement.
        }
        draining = draining.concat(previous)
        publish()
    }
    function detachCompletedProfile() {
        if (pageSaveIntent || items.some(function(entry) { return entry.dependsOnProfile() })) return false
        var retained = []
        for (var entry of items) {
            entry.releaseAuthorization()
            if (entry.completed && entry.fileSnapshot.path) {
                entry.detached = true
                entry.request = null
                entry.owningProfile = null
                retained.push(entry)
            } else { entry.request = null; entry.destroy() }
        }
        items = retained
        idleProfileDetached = true
        publish()
        return true
    }
    function resetProfile() {
        // Settled private files remain ephemeral account-scoped capabilities
        // after the idle engine profile goes away. They are not history rows.
        if (idleProfileDetached && !profile) {
            seenNativeIds = ({})
            ++profileGeneration
            publish()
            return
        }
        var detached = items.filter(function(entry) { return entry.detached })
        items = items.filter(function(entry) { return !entry.detached })
        var keepPanel = panelOpen && detached.length > 0
        retire("profile-replaced")
        for (var entry of draining) { entry.request = null; entry.destroy() }
        draining = []
        seenNativeIds = ({})
        ++profileGeneration
        pageSaveIntent = null
        rejectedPageRequest = null
        rejectedPageReason = ""
        nativeSaveStateChanged()
        retired = false
        idleProfileDetached = false
        items = detached
        panelOpen = keepPanel
        registerOwner()
        publish()
    }
    onProfileChanged: resetProfile()
    onAccountStoreChanged: if (componentReady) retire("account-replaced")
    onPolicyChanged: if (componentReady && !retired) registerOwner()
    onActiveChanged: if (!active && !idleProfileDetached) retire("inactive")
    Component.onCompleted: { componentReady = true; registerOwner() }
    Component.onDestruction: {
        retire("destroyed")
        if (policy && typeof policy.unregisterDownloadOwner === "function") policy.unregisterDownloadOwner(root)
    }
    Connections {
        target: root.rejectedPageRequest
        ignoreUnknownSignals: true
        function onStateChanged() { Qt.callLater(root.settleRejectedPageOffer) }
        function onIsFinishedChanged() { Qt.callLater(root.settleRejectedPageOffer) }
    }
    Connections {
        target: root.policy
        ignoreUnknownSignals: true
        function onDownloadAccountScopeChanged() {
            if (!root.componentReady) return
            root.retire("account-scope-changed")
            // Registration of the next profile follows native scope binding.
            // No settled old-account capability is relabelled into that scope.
        }
    }
    Connections {
        target: root.profile
        ignoreUnknownSignals: true
        function onDownloadRequested(request) { root.receive(request) }
    }
    Component {
        id: downloadEntry
        QtObject {
            id: entry
            required property var request
            property bool entryReady: false
            Component.onCompleted: entryReady = true
            property var source: ({})
            property string downloadId: ""
            // Adapter instance binding, distinct from source.profileId supplied
            // by SessionStore only when that source is actually known.
            property string profileId: ""
            property var accountScope: ({})
            property string nativeId: ""
            property string decisionToken: ""
            property bool decisionConsumed: false
            property var admission: ({})
            property var fileSnapshot: ({})
            property var actionResults: ({})
            property var metadataStore: null
            property var owningProfile: null
            property string downloadToken: ""
            property bool privateDownload: false
            property string sessionId: ""
            property string visitToken: ""
            property string sourceUrl: ""
            property string transferKind: "resource"
            property string interruption: ""
            property bool invalidated: false
            property bool detached: false
            property bool requestLost: false
            property bool cancelledTerminal: false
            property int nativeState: WebEngineDownloadRequest.DownloadRequested
            property bool nativeFinished: false
            property bool recorded: false
            property string metadataState: "pending"
            readonly property bool metadataPending: metadataState === "retryable"
            property bool notified: false
            property bool settlePending: false
            property bool cancellationRequested: false
            property string name: ""
            property string directory: ""
            readonly property string path: admission.path ? String(admission.path) : directory.replace(/[\\/]$/, "") + "/" + name
            property real received: 0
            property real total: -1
            property int state: WebEngineDownloadRequest.DownloadRequested
            property bool paused: false
            property bool finished: false
            readonly property bool completed: !cancelledTerminal && state === WebEngineDownloadRequest.DownloadCompleted
            readonly property bool canPause: !!request && !invalidated && !cancelledTerminal && !request.isFinished && request.state === WebEngineDownloadRequest.DownloadInProgress && !request.isPaused
            readonly property bool canResume: !!request && !invalidated && !cancelledTerminal && !request.isFinished && (request.isPaused || request.state === WebEngineDownloadRequest.DownloadInterrupted)
            readonly property bool canCancel: !!request && !invalidated && !cancelledTerminal && !request.isFinished && request.state !== WebEngineDownloadRequest.DownloadCompleted && request.state !== WebEngineDownloadRequest.DownloadCancelled
            readonly property string terminalOutcome: invalidated ? "invalidated" : requestLost ? "failed"
                : cancelledTerminal ? "cancelled" : completed ? "succeeded"
                : state === WebEngineDownloadRequest.DownloadCancelled ? "cancelled"
                : state === WebEngineDownloadRequest.DownloadInterrupted && finished ? "failed" : ""
            readonly property string status: invalidated ? qsTr("Retired")
                : requestLost ? qsTr("Download no longer available")
                : cancelledTerminal ? qsTr("Canceled") : completed ? qsTr("Completed")
                : state === WebEngineDownloadRequest.DownloadRequested ? qsTr("Waiting for destination")
                : state === WebEngineDownloadRequest.DownloadCancelled ? qsTr("Canceled")
                : state === WebEngineDownloadRequest.DownloadInterrupted ? qsTr("Interrupted")
                : paused ? qsTr("Paused") : qsTr("Downloading")
            function markRequestLost() {
                if (requestLost || detached || invalidated || finished || completed) return
                requestLost = true
                request = null
                settlePending = false
                releaseAuthorization()
                metadataState = "invalidated"
                root.failNativeSaveDownload(downloadId, "native-request-lost")
                root.publish()
            }
            onRequestChanged: if (entryReady && !request) markRequestLost()
            function observeNativeState() {
                if (!request || cancelledTerminal) return
                if (request.state === WebEngineDownloadRequest.DownloadCancelled) {
                    cancelledTerminal = true
                    fileSnapshot = ({})
                    releaseAuthorization()
                    metadataState = "cancelled"
                }
            }
            function dependsOnProfile() {
                return !!request && (settlePending || !finished)
            }
            function snapshot() {
                return {downloadId: downloadId, requestToken: downloadId, decisionToken: decisionConsumed ? "" : decisionToken,
                    name: name, source: JSON.parse(JSON.stringify(source)), sourceAvailable: !!sessionId, sourceStatus: source.sourceStatus || (sessionId ? "captured" : "unavailable"),
                    sourceBindingCapability: root.sourceBindingCapability,
                    privateDownload: privateDownload, profileInstanceId: profileId,
                    accountScope: JSON.parse(JSON.stringify(accountScope)),
                    engineId: "qtwebengine", state: cancelledTerminal ? WebEngineDownloadRequest.DownloadCancelled : state,
                    nativeState: nativeState, nativeFinished: nativeFinished, nativeAvailable: !!request,
                    requestLost: requestLost, status: status,
                    terminal: terminalOutcome, terminalOutcome: terminalOutcome,
                    received: received, total: total,
                    totalKnown: total >= 0, indeterminate: total <= 0 && !finished && terminalOutcome.length === 0,
                    paused: paused, finished: finished, completed: completed,
                    metadataState: metadataState, canPause: canPause, canResume: canResume,
                    resumeGuarantee: false, canCancel: canCancel,
                    cancellationRequested: cancellationRequested, cleanupConfirmed: false,
                    canOpen: completed && !invalidated && !!fileSnapshot.path,
                    interruption: interruption, transferKind: transferKind,
                    documentDependency: transferKind === "pageSerialization" && !finished,
                    openCapability: "owner-resolved-file", revealCapability: "containing-folder-only"}
            }
            function acceptDecision(token, decision) {
                if (!request || invalidated || decisionConsumed || token !== decisionToken
                        || request.state !== WebEngineDownloadRequest.DownloadRequested)
                    return {status: "invalidated"}
                if (decision.cancel === true) {
                    decisionConsumed = true
                    cancellationRequested = true
                    request.cancel()
                    scheduleUpdate()
                    return {status: "cancelled"}
                }
                var directory = decision.directory === undefined ? request.downloadDirectory : String(decision.directory)
                var name = decision.fileName === undefined ? request.downloadFileName : String(decision.fileName)
                if (root.policy) {
                    var approved = root.policy.admitDownloadDestination(directory, name)
                    if (approved.status !== "admitted") return {status: "rejected", reason: "destination"}
                    admission = approved
                    directory = admission.directory
                    name = admission.fileName
                } else if (decision.directory !== undefined || decision.fileName !== undefined) {
                    return {status: "unsupported", reason: "destination-policy-unavailable"}
                }
                request.downloadDirectory = directory
                request.downloadFileName = name
                entry.directory = directory
                entry.name = name
                decisionConsumed = true
                if (!privateDownload && sessionId.length && visitToken.length && metadataStore
                        && typeof metadataStore.authorizeBrowserDownload === "function") {
                    downloadToken = metadataStore.authorizeBrowserDownload(sessionId, visitToken,
                        {id: downloadId, profileId: profileId, nativeId: nativeId,
                            sourceUrl: sourceUrl, path: path, admission: admission})
                }
                // Destination admission and the history grant remain independent.
                request.accept()
                scheduleUpdate()
                return {status: "dispatched"}
            }
            function scheduleUpdate() {
                if (settlePending) return
                settlePending = true
                Qt.callLater(update)
            }
            function update() {
                settlePending = false
                if (!request) { root.publish(); return }
                name = request.downloadFileName
                directory = request.downloadDirectory
                received = request.receivedBytes
                total = request.totalBytes
                nativeState = request.state
                nativeFinished = request.isFinished
                observeNativeState()
                state = cancelledTerminal ? WebEngineDownloadRequest.DownloadCancelled : nativeState
                paused = request.isPaused
                finished = nativeFinished
                interruption = String(request.interruptReasonString || "")
                if (invalidated || owningProfile !== root.profile || metadataStore !== root.accountStore || root.retired) {
                    releaseAuthorization()
                    metadataState = "invalidated"
                    root.publish()
                    return
                }
                if (state === WebEngineDownloadRequest.DownloadCancelled) {
                    releaseAuthorization()
                    metadataState = "cancelled"
                }
                if (completed && !recorded && metadataState !== "retryable"
                        && metadataState !== "rejected" && metadataState !== "invalidated") {
                    // Qt emits stateChanged(Completed) before the final byte
                    // properties. This callback runs after that native update.
                    if (!notified) { notified = true; root.message(qsTr("Downloaded %1.").arg(name)) }
                    if (root.policy && !fileSnapshot.path)
                        fileSnapshot = root.policy.completedDownloadFile(admission)
                    commitMetadata()
                }
                root.settleNativeSave(entry)
                root.pruneFinished()
                root.publish()
            }
            function commitMetadata() {
                if (recorded || invalidated || cancelledTerminal || !completed || !request) return
                if (privateDownload || !downloadToken.length) {
                    metadataState = privateDownload ? "private" : "blockedSourceIdentity"
                    recorded = true
                    return
                }
                if (!root.active || root.retired || owningProfile !== root.profile || metadataStore !== root.accountStore) {
                    metadataState = "invalidated"
                    releaseAuthorization()
                    return
                }
                var descriptor = {id: downloadId, profileId: profileId, nativeId: nativeId,
                    url: sourceUrl, path: path, fileName: name, mimeType: request.mimeType,
                    receivedBytes: request.receivedBytes, totalBytes: request.totalBytes,
                    state: "completed", sessionId: sessionId, visitToken: visitToken,
                    downloadToken: downloadToken, fileSnapshot: fileSnapshot}
                var result = typeof metadataStore.recordBrowserDownloadResult === "function"
                    ? metadataStore.recordBrowserDownloadResult(descriptor) : {status: "rejected", reason: "typed-owner-required"}
                metadataState = result.status
                if (metadataState === "recorded") { recorded = true; releaseAuthorization() }
                else if (metadataState === "retryable")
                    root.message(qsTr("The file downloaded, but its history could not be saved. Retry saving history."))
                else releaseAuthorization()
            }
            function releaseAuthorization() {
                if (downloadToken.length && metadataStore && typeof metadataStore.cancelBrowserDownload === "function")
                    metadataStore.cancelBrowserDownload(downloadToken)
                downloadToken = ""
            }
            function performAction(operationId, action) {
                if (actionResults[operationId])
                    return actionResults[operationId].action === action ? actionResults[operationId] : {status: "invalidated"}
                var allowed = action === "pause" ? canPause : action === "resume" ? canResume : action === "cancel" ? canCancel : false
                if (!allowed) return {status: "blocked", action: action}
                var result = {status: "dispatched", action: action}
                actionResults[operationId] = result
                if (action === "pause") request.pause()
                else if (action === "resume") request.resume()
                else { cancellationRequested = true; request.cancel() }
                scheduleUpdate()
                return result
            }
            // Adapter compatibility helpers. Product controls send only IDs.
            function togglePause() { return performAction(downloadId + ":compat:" + (++root.requestSerial), paused || state === WebEngineDownloadRequest.DownloadInterrupted ? "resume" : "pause") }
            function cancel() { return performAction(downloadId + ":compat:" + (++root.requestSerial), "cancel") }
            property var observer: Connections {
                target: entry.request
                ignoreUnknownSignals: true
                function onStateChanged() { entry.observeNativeState(); entry.scheduleUpdate() }
                function onIsFinishedChanged() { entry.scheduleUpdate() }
                function onReceivedBytesChanged() { entry.scheduleUpdate() }
                function onTotalBytesChanged() { entry.scheduleUpdate() }
                function onIsPausedChanged() { entry.scheduleUpdate() }
                function onInterruptReasonChanged() { entry.scheduleUpdate() }
                function onDestroyed() { entry.markRequestLost(); entry.request = null; entry.releaseAuthorization(); root.publish() }
            }
        }
    }
    // App-style page supplied by the shell; it replaces the page viewport rather
    // than drawing over a live website or opening a generic Browser Tools panel.
    Rectangle {
        id: panel
        objectName: "feriaBrowserDownloadsPage"
        parent: root.panelSurface || root
        anchors.fill: parent
        visible: root.panelOpen && root.panelSurface !== null
        color: "#202027"
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 12
            RowLayout {
                Text { Layout.fillWidth: true; text: qsTr("Downloads"); color: theme.ink; font.family: theme.ui; font.pixelSize: 24 }
                Account.AccountButton { text: qsTr("Close"); onClicked: root.panelOpen = false }
            }
            Text {
                visible: root.rows.length === 0 && root.history.length === 0
                text: qsTr("Your downloads will appear here.")
                color: theme.inkDim; font.family: theme.ui
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 16
                model: root.rows
                delegate: ColumnLayout {
                    required property var modelData
                    width: ListView.view.width
                    Text { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.ink; font.family: theme.ui }
                    Text { text: modelData.status; color: theme.inkDim; font.family: theme.ui }
                    Text { visible: modelData.metadataState === "blockedSourceIdentity"; text: qsTr("File saved. Source history is unavailable on this engine."); color: theme.inkDim; font.family: theme.ui }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, modelData.total); value: modelData.received; indeterminate: modelData.indeterminate }
                    RowLayout {
                        Account.AccountButton { visible: modelData.metadataState === "retryable"; text: qsTr("Retry saving history"); onClicked: root.retryMetadata(modelData.downloadId) }
                        Account.AccountButton { visible: modelData.canPause || modelData.canResume; text: modelData.canResume ? qsTr("Try resume") : qsTr("Pause"); onClicked: root.requestDownloadAction(modelData.downloadId, modelData.downloadId + ":ui:" + (++root.requestSerial), modelData.canResume ? "resume" : "pause") }
                        Account.AccountButton { visible: modelData.canCancel; text: qsTr("Cancel"); onClicked: root.requestDownloadAction(modelData.downloadId, modelData.downloadId + ":ui:" + (++root.requestSerial), "cancel") }
                        Account.AccountButton { visible: modelData.canOpen; text: qsTr("Open"); onClicked: root.fileAction(modelData.downloadId, false, false) }
                        Account.AccountButton { visible: modelData.canOpen; text: qsTr("Open folder"); onClicked: root.fileAction(modelData.downloadId, false, true) }
                        Account.AccountButton { visible: !!modelData.decisionToken; text: qsTr("Save"); onClicked: root.decideDownload(modelData.downloadId, modelData.decisionToken, {}) }
                        Account.AccountButton { visible: !!modelData.decisionToken; text: qsTr("Cancel save"); onClicked: root.decideDownload(modelData.downloadId, modelData.decisionToken, {cancel:true}) }
                    }
                }
                footer: Column {
                    width: parent.width; spacing: 8
                    Repeater {
                        model: root.history
                        delegate: RowLayout {
                            required property var modelData
                            width: parent.width
                            Text { Layout.fillWidth: true; text: modelData.fileName || qsTr("Downloaded file"); textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.inkDim; font.family: theme.ui }
                            Account.AccountButton { text: qsTr("Open"); onClicked: root.fileAction(String(modelData.id || ""), true, false) }
                            Account.AccountButton { text: qsTr("Open folder"); onClicked: root.fileAction(String(modelData.id || ""), true, true) }
                        }
                    }
                }
            }
        }
    }
}
