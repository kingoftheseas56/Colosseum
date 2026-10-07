import QtQuick
import QtWebEngine

// Exact-document adapter. SessionStore owns the transaction and closed records;
// this object owns only the originating Qt request and presentation projection.
Item {
    id: controller
    required property var browser
    required property var sessions
    required property string sessionId
    property bool retirementAllowed: true
    signal blocked(string reason)
    property var identity: null
    property string attempt: ""
    property var consent: null
    property var request: null
    property bool awaitingRejection: false
    property bool staleAcknowledgementFence: false
    property string failure: ""
    readonly property bool pending: attempt.length > 0
    signal changed()

    function matches() {
        if (!identity) return false
        var current = sessions.browserIdentity(sessionId)
        for (var key of Object.keys(identity)) if (current[key] !== identity[key]) return false
        return current.spaceId === sessionId
    }
    function clearLocal() {
        rejectionDeadline.stop()
        attempt = ""; identity = null; request = null; consent = null
        awaitingRejection = false
        changed()
    }
    function begin(dispatch) {
        if (!retirementAllowed) {
            blocked("Close this space's child windows first. This Qt runtime cannot safely retire their creation view yet.")
            return false
        }
        if (!browser || browser.closingSession || pending || failure.length) return false
        identity = sessions.browserIdentity(sessionId)
        attempt = sessions.beginBrowserClose(identity)
        if (!attempt.length) { identity = null; return false }
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
        if (browser.closingSession || failure.length || (!pending && !begin(false))) {
            dialog.dialogReject()
            return
        }
        if (request || !matches()) { dialog.dialogReject(); return }
        request = dialog
        consent = {identity: Object.assign({}, identity), attempt: attempt,
            origin: browser.browserRequests.originLabel(dialog.securityOrigin), message: dialog.message}
        changed()
    }
    function answer(token, leave) {
        if (!request || !consent || attempt !== token || !matches()) return false
        var dialog = request
        request = null; consent = null
        // Set the guard BEFORE calling Qt: its rejection hook can be synchronous.
        awaitingRejection = !leave
        if (!leave) rejectionDeadline.restart()
        changed()
        if (leave) dialog.dialogAccept()
        else dialog.dialogReject()
        return true
    }
    function rejectionAcknowledged() {
        if (!pending || !awaitingRejection || !matches()) return
        sessions.settleBrowserClose(identity, attempt, false, {})
        clearLocal()
    }
    function acknowledge() {
        if (!browser || browser.closingSession) return
        // An old engine event has no public attempt argument. Never reinterpret
        // an invalidated app-close acknowledgement as a fresh site close.
        if (!pending && staleAcknowledgementFence) return
        // window.close() without app intent has already traversed Chromium's
        // veto path. Mint a token for that exact view, never the selected space.
        if (!pending && !begin(false)) return
        if (!matches() || awaitingRejection || request) return
        if (!retirementAllowed) {
            invalidate(false)
            blocked("Close this space's child windows first. Their creation view is still required by this Qt runtime.")
            return
        }
        var key = identity, token = attempt
        var descriptor = {url: String(browser.url), title: browser.title}
        clearLocal()
        sessions.settleBrowserClose(key, token, true, descriptor)
    }
    function invalidate(advanceDocument) {
        var dialog = request
        var oldIdentity = identity, oldAttempt = attempt
        if (pending) staleAcknowledgementFence = true
        clearLocal()
        if (dialog) dialog.dialogReject()
        // Cancellation is an identity boundary, not a claimed renderer veto
        // acknowledgement. Invalidate pending work before a navigation command
        // (or on the committed load) so delayed callbacks cannot gain authority.
        if (advanceDocument || oldAttempt) sessions.advanceBrowserDocument(sessionId)
        if (advanceDocument) failure = ""
    }
    Timer {
        id: rejectionDeadline
        interval: 5000
        onTriggered: {
            if (!controller.pending || !controller.awaitingRejection) return
            // Failure, NEVER timeout-as-success or a claimed renderer veto.
            // Preserve the document and refuse retries until navigation clears
            // the native token through an explicit identity boundary.
            controller.failure = "This Qt runtime did not acknowledge cancellation. Navigate or reload before retrying close."
            controller.staleAcknowledgementFence = true
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
