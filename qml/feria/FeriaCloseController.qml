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
    property string failure: ""
    readonly property bool pending: attempt.length > 0
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
    function clearLocal() {
        rejectionDeadline.stop()
        attempt = ""; identity = null; request = null; consent = null
        awaitingRejection = false; cancellationRequested = false; acceptedLeave = false
        changed()
    }
    function begin(dispatch) {
        if (!retirementAllowed) {
            blocked("Close this space's child windows first. This Qt runtime still needs their creation view.")
            return false
        }
        if (!browser || browser.closingSession || pending || request || externalAccepted
            || retiredBarrier || failure.length) return false
        identity = sessions.browserIdentity(sessionId)
        attempt = sessions.beginBrowserClose(identity)
        if (!attempt.length) { identity = null; return false }
        // Only a completed cancellation barrier permits admission here.
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
        if (browser.closingSession || request || failure.length) { dialog.dialogReject(); return }
        if (pending) {
            if (!matches()) { dialog.dialogReject(); return }
            if (cancellationRequested) {
                awaitingRejection = true
                rejectionDeadline.restart()
                dialog.dialogReject()
                return
            }
            request = dialog
            consent = {identity: Object.assign({}, identity), attempt: attempt, kind: "close",
                origin: browser.browserRequests.originLabel(dialog.securityOrigin)}
        } else {
            // Navigation/reload and site window.close can both request unload.
            // Without app intent this is consent only, not a pending close. A
            // later exact windowCloseRequested distinguishes terminal site close.
            var key = sessions.browserIdentity(sessionId)
            if (!key.spaceId) { dialog.dialogReject(); return }
            if (!retiredBarrier) staleAcknowledgementFence = false
            request = dialog
            consent = {identity: key, attempt: "unload:" + (++externalSerial), kind: "unload",
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
        staleAcknowledgementFence = cancelled
        clearLocal()
        if (cancelled) cancellationSettled()
    }
    function finishRetiredBarrier() {
        if (retiredAttempt && identityMatches(retiredIdentity))
            sessions.settleBrowserClose(retiredIdentity, retiredAttempt, false, {})
        retiredBarrier = false; retiredIdentity = null; retiredAttempt = ""; failure = ""
        rejectionDeadline.stop()
    }
    function rejectionAcknowledged() {
        if (retiredBarrier && !pending) { finishRetiredBarrier(); return }
        if (!pending || !awaitingRejection || !matches()) return
        settleCancellation()
    }
    function acknowledge() {
        if (!browser || browser.closingSession) return
        if (retiredBarrier && !pending) { finishRetiredBarrier(); return }
        if (pending && cancellationRequested) {
            // The old request finished without a dialog. Qt's terminal event is
            // an observed barrier; preserve the page and honor queued navigation.
            settleCancellation()
            return
        }
        if (!pending && staleAcknowledgementFence) return
        if (!pending && externalAccepted) {
            if (!identityMatches(externalAccepted)) { externalAccepted = null; return }
            externalAccepted = null
        }
        if (!pending && !begin(false)) return
        if (!matches() || awaitingRejection || request) return
        if (!retirementAllowed) {
            invalidate(false)
            blocked("Close the child windows before retiring their creation view.")
            return
        }
        var key = identity, token = attempt
        var descriptor = {url: String(browser.url), title: browser.title}
        clearLocal()
        sessions.settleBrowserClose(key, token, true, descriptor)
    }
    function invalidate(advanceDocument) {
        var dialog = request
        var hadAttempt = pending, hadExternal = externalAccepted !== null
        if (hadAttempt) {
            retiredIdentity = identity; retiredAttempt = attempt
            retiredBarrier = true
        }
        if (hadAttempt || hadExternal) staleAcknowledgementFence = true
        externalAccepted = null
        clearLocal()
        if (advanceDocument || hadAttempt) sessions.advanceBrowserDocument(sessionId)
        if (dialog) dialog.dialogReject()
        if (retiredBarrier) rejectionDeadline.restart()
        if (advanceDocument && !retiredBarrier) failure = ""
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
        function onRenderProcessTerminated(status, code) { controller.invalidate(true) }
    }
    Component.onDestruction: if (request) request.dialogReject()
}
