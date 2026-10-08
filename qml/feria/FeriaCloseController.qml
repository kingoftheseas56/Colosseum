import QtQuick
import QtWebEngine

// SessionStore owns close/reopen state. This adapter holds only exact-origin Qt
// requests. A new attempt cannot cross an unresolved engine cancellation barrier.
Item {
    id: controller
    required property var browser
    required property var sessions
    required property string sessionId
    property bool retirementAllowed: true
    property var identity: null
    property string attempt: ""
    property var consent: null
    property var request: null
    property bool awaitingRejection: false
    property bool cancellationRequested: false
    property bool acceptedLeave: false
    property bool staleAcknowledgementFence: false
    property bool retiredBarrier: false
    property var retiredIdentity: null
    property string retiredAttempt: ""
    property int externalSerial: 0
    property var externalAccepted: null
    property var externalRejection: null
    property bool externalRecoveryOnly: false
    property bool retryExternalCancellation: false
    property int recoveryAfterGeneration: -1
    property int attemptLoadGeneration: -1
    property var closeAcknowledgement: null
    property bool engineClosed: false
    property string failure: ""
    readonly property bool pending: attempt.length > 0
    readonly property bool busy: pending || request !== null || externalAccepted !== null || externalRejection !== null || retiredBarrier || staleAcknowledgementFence || failure.length > 0
    signal changed()
    signal blocked(string reason)
    signal cancellationSettled()

    function identityMatches(key) {
        if (!key) return false
        var current = sessions.browserIdentity(sessionId)
        for (var name of Object.keys(key)) if (current[name] !== key[name]) return false
        return current.spaceId === sessionId
    }
    function matches() { return identityMatches(identity) }
    function canRetryExternalCancellation() {
        return externalRejection !== null && !externalRecoveryOnly && !pending
            && !request && !externalAccepted && !retiredBarrier && !staleAcknowledgementFence
            && identityMatches(externalRejection.identity)
    }
    function canTransfer() { return !busy || canRetryExternalCancellation() }
    function hostTransferred(key) {
        if (externalRejection) externalRejection.identity = Object.assign({}, key)
    }
    function clearLocal() {
        rejectionDeadline.stop()
        attempt = ""; identity = null; request = null; consent = null; closeAcknowledgement = null
        awaitingRejection = false; cancellationRequested = false; acceptedLeave = false
        retryExternalCancellation = false
        changed()
    }
    function begin(dispatch) {
        if (!retirementAllowed) {
            blocked("Close this space's child windows first. This Qt runtime still needs their creation view.")
            return false
        }
        var retryExternal = canRetryExternalCancellation()
        if (!browser || browser.closingSession || (busy && !retryExternal)) return false
        var key = sessions.browserIdentity(sessionId)
        var token = sessions.beginBrowserClose(key)
        if (!token.length) return false
        if (retryExternal) {
            externalRejection = null; failure = ""; externalRecoveryOnly = false
        }
        identity = key
        attempt = token
        attemptLoadGeneration = browser.navigationGeneration
        retryExternalCancellation = retryExternal
        var capturedIdentity = Object.assign({}, identity), capturedAttempt = attempt
        closeAcknowledgement = function() {
            if (controller.attempt !== capturedAttempt || !controller.identityMatches(capturedIdentity)) return
            controller.acknowledgeAttempt()
        }
        // New explicit intent may follow an external Stay; its own terminal
        // callback is still required. Other unresolved barriers block admission.
        staleAcknowledgementFence = false
        changed()
        if (dispatch !== false) {
            browser.wakePage()
            browser.triggerWebAction(WebEngineView.RequestClose)
        }
        return true
    }
    function capture(dialog) {
        if (dialog.type !== JavaScriptDialogRequest.DialogTypeBeforeUnload) return
        dialog.accepted = true
        if (browser.closingSession || request
            || (failure.length && !externalRejection && !staleAcknowledgementFence)) { dialog.dialogReject(); return }
        if (pending) {
            if (!matches()) { dialog.dialogReject(); return }
            if (cancellationRequested) {
                awaitingRejection = true
                rejectionDeadline.restart()
                dialog.dialogReject()
                return
            }
            request = dialog
            retryExternalCancellation = false
            consent = {identity: Object.assign({}, identity), attempt: attempt, kind: "close",
                origin: browser.browserRequests.originLabel(dialog.securityOrigin)}
        } else {
            // Navigation/reload and site window.close can both request unload.
            // Without app intent this is consent only, not a pending close. A
            // later exact windowCloseRequested distinguishes terminal site close.
            var key = sessions.browserIdentity(sessionId)
            if (!key.spaceId) { dialog.dialogReject(); return }
            request = dialog
            consent = {identity: key, attempt: "unload:" + (++externalSerial), kind: "unload",
                loadGeneration: browser.navigationGeneration,
                origin: browser.browserRequests.originLabel(dialog.securityOrigin)}
        }
        changed()
    }
    function answer(token, leave) {
        if (!request || !consent || consent.attempt !== token || !identityMatches(consent.identity)) return false
        var dialog = request, ticket = consent
        request = null; consent = null
        if (ticket.kind === "unload") {
            externalAccepted = leave ? ticket.identity : null
            if (!leave) {
                externalRejection = ticket
                recoveryAfterGeneration = ticket.loadGeneration
                // Do not pretend the dialog answer is a renderer ACK. A new
                // explicit close owns its own terminal route; a visual transfer
                // preserves this live view and cannot approve destruction.
                failure = ""
            }
            changed()
            if (leave) dialog.dialogAccept()
            else dialog.dialogReject()
            return true
        }
        if (attempt !== token || !matches()) return false
        acceptedLeave = leave
        awaitingRejection = !leave
        if (!leave) rejectionDeadline.restart()
        changed()
        if (leave) dialog.dialogAccept()
        else dialog.dialogReject()
        return true
    }
    function cancelForNavigation() {
        if (!pending) return !retiredBarrier && !externalAccepted
        // Once Leave was accepted, dispatch no replacement document until the
        // close completes. Its old callback cannot gain a fresh document target.
        if (acceptedLeave || failure.length) return false
        cancellationRequested = true
        staleAcknowledgementFence = true
        awaitingRejection = true
        rejectionDeadline.restart()
        var dialog = request
        request = null; consent = null
        changed()
        if (dialog) dialog.dialogReject()
        return true
    }
    function settleCancellation() {
        var cancelled = cancellationRequested
        if (matches()) sessions.settleBrowserClose(identity, attempt, false, {})
        retiredBarrier = false
        staleAcknowledgementFence = false // actual old-operation terminal, not elapsed time
        clearLocal()
        if (cancelled) cancellationSettled()
    }
    function finishRetiredBarrier() {
        if (retiredAttempt && identityMatches(retiredIdentity))
            sessions.settleBrowserClose(retiredIdentity, retiredAttempt, false, {})
        retiredBarrier = false; retiredIdentity = null; retiredAttempt = ""; failure = ""
        staleAcknowledgementFence = false
        rejectionDeadline.stop()
        changed()
    }
    function rejectionAcknowledged() {
        if (pending && matches() && retryExternalCancellation && !request && !awaitingRejection) {
            // Chromium may coalesce an explicit user close with the cancelled
            // navigation. Its real close rejection clears the old engine state.
            // Retry once after that terminal callback unwinds, preserving the
            // same captured intent. No timeout or JS result authorizes this.
            retryExternalCancellation = false
            var token = attempt, key = Object.assign({}, identity)
            Qt.callLater(function() {
                if (controller.attempt !== token || !controller.identityMatches(key)
                    || controller.request || !controller.browser || controller.browser.closingSession) return
                controller.browser.triggerWebAction(WebEngineView.RequestClose)
            })
            return
        }
        if (externalRejection && !externalRecoveryOnly) {
            externalRejection = null; failure = ""; staleAcknowledgementFence = false
            changed()
            return
        }
        if (retiredBarrier && !pending) { finishRetiredBarrier(); return }
        if (!pending || request || !matches()) return
        settleCancellation()
    }
    function prepareNavigation() {
        if (externalAccepted || request) return false
        // If navigation overlaps a rejected external operation, a later untagged
        // rejection cannot safely identify the newest ticket. Require a new
        // document instead; never promote such an overlap to close permission.
        if (externalRejection) externalRecoveryOnly = true
        return true
    }
    function acknowledgeAttempt() {
        if (!pending || !matches()) return
        if (cancellationRequested) { settleCancellation(); return }
        if (awaitingRejection || request) return
        if (!retirementAllowed) {
            invalidate(false)
            blocked("Close the child windows before retiring their creation view.")
            return
        }
        var key = identity, token = attempt
        var descriptor = {url: String(browser.url), title: browser.title}
        engineClosed = true
        clearLocal()
        sessions.settleBrowserClose(key, token, true, descriptor)
    }
    function acknowledge() {
        if (!browser || browser.closingSession) return
        if (pending) { if (closeAcknowledgement) closeAcknowledgement(); return }
        if (retiredBarrier) { finishRetiredBarrier(); return }
        if (externalRejection || staleAcknowledgementFence) return
        if (externalAccepted) {
            if (!identityMatches(externalAccepted)) { externalAccepted = null; return }
            externalAccepted = null
        }
        if (begin(false)) acknowledgeAttempt()
    }
    function rendererTerminated() {
        // A dead renderer cannot acknowledge rejection later. Preserve canonical
        // space, settle its pending attempt false, and invalidate old callbacks.
        if (pending && matches()) sessions.settleBrowserClose(identity, attempt, false, {})
        if (retiredAttempt && identityMatches(retiredIdentity))
            sessions.settleBrowserClose(retiredIdentity, retiredAttempt, false, {})
        var dialog = request
        clearLocal()
        externalAccepted = null; externalRejection = null; externalRecoveryOnly = false
        retiredBarrier = false; retiredIdentity = null; retiredAttempt = ""
        staleAcknowledgementFence = false; failure = ""; recoveryAfterGeneration = -1
        sessions.advanceBrowserDocument(sessionId)
        if (dialog) dialog.dialogReject()
        changed()
    }
    function documentReplaced(generation) {
        // This signal is LoadStarted -> LoadSucceeded for a real main document,
        // never a fragment, provisional start, stopped or failed load.
        if (generation <= recoveryAfterGeneration) return
        if (!externalRejection && !retiredBarrier && !staleAcknowledgementFence) return
        externalAccepted = null; externalRejection = null; externalRecoveryOnly = false
        finishRetiredBarrier()
        recoveryAfterGeneration = -1
    }
    function invalidate(advanceDocument) {
        var dialog = request
        var hadAttempt = pending, hadExternal = externalAccepted !== null
        if (hadAttempt) {
            retiredIdentity = identity; retiredAttempt = attempt
            retiredBarrier = true
            recoveryAfterGeneration = attemptLoadGeneration
        }
        if (hadExternal) {
            recoveryAfterGeneration = browser.navigationGeneration - 1
            failure = "Close and host transfer wait for a successful replacement page. If loading fails or stops, navigate or reload to recover."
        }
        if (hadAttempt || hadExternal) staleAcknowledgementFence = true
        externalAccepted = null
        clearLocal()
        if (advanceDocument || hadAttempt) sessions.advanceBrowserDocument(sessionId)
        if (dialog) dialog.dialogReject()
        if (retiredBarrier) rejectionDeadline.restart()
        if (advanceDocument && !retiredBarrier && !externalRejection && !staleAcknowledgementFence) failure = ""
    }
    Timer {
        id: rejectionDeadline
        interval: 5000
        onTriggered: {
            if (!controller.pending && !controller.retiredBarrier) return
            // Unsupported state, never timeout success. No retry until a verified
            // engine boundary/profile retirement; no hidden pending UI spinner.
            controller.failure = "This Qt runtime did not acknowledge cancellation. Close is unavailable for this page until its pending engine operation finishes."
            controller.staleAcknowledgementFence = true
            controller.retiredBarrier = true
            if (controller.pending) {
                controller.retiredIdentity = controller.identity
                controller.retiredAttempt = controller.attempt
                controller.recoveryAfterGeneration = controller.attemptLoadGeneration
            }
            controller.clearLocal()
        }
    }
    Connections {
        target: controller.browser
        function onJavaScriptDialogRequested(dialog) { controller.capture(dialog) }
        function onCloseRejected() { controller.rejectionAcknowledged() }
        function onWindowCloseRequested() { controller.acknowledge() }
        function onStarted(location) { controller.invalidate(true) }
        function onDocumentReplaced(generation) { controller.documentReplaced(generation) }
        function onRenderProcessTerminated(status, code) { controller.rendererTerminated() }
    }
    Component.onDestruction: if (request) request.dialogReject()
}
