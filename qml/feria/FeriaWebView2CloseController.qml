import QtQuick

// WebView2 per-space consented-close controller. Same deck-facing surface as
// FeriaCloseController, but built on the WebView2 semantics the native probe
// established: Stay is the retained ScriptDialogOpening deferral completed
// without Accept (no navigation id exists for the denied attempt); Leave
// accepts and completes, then stays pending until the host proves correlated
// navigation completion and actual departure of the original document. All
// identity/outcome fencing runs through the shared FeriaBrowserOperations
// seam; this file holds no canonical state.
Item {
    id: controller
    required property var browser
    required property var sessions
    required property string sessionId
    property bool retirementAllowed: true
    property var identity: null
    property string attempt: ""
    property var consent: null
    property string nativeToken: ""
    property string requestId: ""
    property var capturedDescriptor: null
    property bool awaitingDeparture: false
    property bool competingNavigation: false
    property bool engineClosed: false
    property string failure: ""
    property bool externalLeaveActive: false
    property int externalSerial: 0
    readonly property bool pending: attempt.length > 0
    readonly property bool busy: pending || consent !== null || externalLeaveActive
        || awaitingDeparture || failure.length > 0
    signal changed()
    signal blocked(string reason)
    signal cancellationSettled()

    function operations() { return sessions ? sessions.browserOperations() : null }
    function identityMatches(key) {
        if (!key) return false
        var current = sessions.browserIdentity(sessionId)
        for (var name of Object.keys(key)) if (current[name] !== key[name]) return false
        return current.spaceId === sessionId
    }
    function matches() { return identityMatches(identity) }
    function originLabel(uri) {
        try { return String(new URL(uri).host) } catch (e) { return String(uri) }
    }
    function clearClose() {
        departureDeadline.stop()
        attempt = ""; identity = null; consent = null; nativeToken = ""
        requestId = ""; capturedDescriptor = null; awaitingDeparture = false
        changed()
    }
    function clearExternal() { externalLeaveActive = false; changed() }
    function begin(dispatch) {
        if (!retirementAllowed) {
            blocked("This space still has dependent child windows.")
            return false
        }
        // A prior invalidated attempt's diagnostic does not fence a fresh
        // intent; only live consent/departure state does.
        if (!browser || browser.closingSession || pending || consent !== null
                || awaitingDeparture || externalLeaveActive) return false
        var key = sessions.browserIdentity(sessionId)
        var admitted = operations().beginOperation(key, "webview2", "request-close")
        if (!admitted.admitted) return false
        identity = key
        attempt = admitted.operationId
        // The reopen descriptor is captured before dispatch: after a real
        // departure the document URL is about:blank and must never leak into
        // the user's history, restore offers or this descriptor.
        capturedDescriptor = {url: String(browser.url), title: String(browser.title || "")}
        failure = ""
        changed()
        if (dispatch !== false) browser.requestClose(attempt)
        return true
    }
    function answer(token, leave) {
        if (!consent || consent.attempt !== token) return false
        if (!identityMatches(consent.identity)) {
            // A replacement or sibling cannot answer this request. Deny the
            // native dialog so the renderer is never left waiting.
            if (consent.kind === "close") browser.respondCloseRequest(nativeToken, false)
            else browser.respondUnloadRequest(nativeToken, false)
            return false
        }
        if (consent.kind === "close") {
            var check = operations().answerRequest(requestId, consent.identity)
            if (!check.valid) {
                var staleToken = nativeToken
                consent = null; nativeToken = ""; requestId = ""
                changed()
                browser.respondCloseRequest(staleToken, false)
                return false
            }
            consent = null
            if (leave) {
                awaitingDeparture = true
                departureDeadline.restart()
                browser.respondCloseRequest(nativeToken, true)
            } else {
                browser.respondCloseRequest(nativeToken, false)
                // The explicit Stay decision settles through closeStayRecorded.
            }
            changed()
            return true
        }
        var externalCheck = operations().answerRequest(consent.attempt, consent.identity)
        if (!externalCheck.valid) {
            // Stale/duplicate answer: clear the presented consent so no busy
            // or chrome state survives, then deny the native request so the
            // renderer is never left waiting.
            var staleToken = nativeToken
            consent = null; nativeToken = ""
            changed()
            browser.respondUnloadRequest(staleToken, false)
            return false
        }
        consent = null
        browser.respondUnloadRequest(nativeToken, leave)
        if (leave) externalLeaveActive = true
        changed()
        return true
    }
    function cancelForNavigation() {
        if (!pending) return !busy
        // A competing navigation cannot be mistaken for terminal close proof.
        // The retained dialog is answered as an explicit Stay (the completed
        // deferral is the decision evidence the runtime needs to reap the
        // internal attempt); the deck resumes the navigation once that
        // decision has settled through closeStayRecorded.
        if (consent && nativeToken) {
            consent = null
            competingNavigation = true
            browser.respondCloseRequest(nativeToken, false)
        } else {
            browser.cancelCloseOperation()
            settleCompetingInvalidated()
        }
        return true
    }
    function settleCompetingInvalidated() {
        var key = identity, token = attempt
        clearClose()
        operations().settleOperation(key, token, "invalidated", "competing-navigation", "", {})
        cancellationSettled()
    }
    function prepareNavigation() {
        // An open consent owns the next unload decision; a Leave already in
        // flight must finish departing before new work is admitted.
        return !consent && !awaitingDeparture
    }
    function invalidate(advanceDocument) {
        if (pending) {
            operations().settleOperation(identity, attempt, "invalidated", "invalidated", "", {})
            if (browser) browser.cancelCloseOperation()
        }
        if (advanceDocument) sessions.advanceBrowserDocument(sessionId)
        clearClose()
        clearExternal()
    }
    function hostTransferred(key) { /* canTransfer gates transfers to idle */ }
    function canTransfer() { return !busy }
    // An accepted Leave has no synthetic terminal: a real correlated
    // departure is the only success. If the native callback never arrives,
    // the operation settles blocked after this bounded deadline and the
    // space is preserved for recovery; native drain stays with the host.
    property int departureDeadlineMs: 30000
    function departBlocked() {
        if (!awaitingDeparture) return
        var key = identity, op = attempt
        clearClose()
        operations().settleOperation(key, op, "blocked",
            "departure-terminal-not-observed", "deadline-elapsed-no-departure", {})
        failure = "The page's departure was not confirmed. The space is unchanged; try closing again."
        changed()
    }
    function rendererLost(kind) {
        // The host drained its native dialogs; settle truthfully and keep the
        // canonical space alive for recovery. A browser-process exit killed
        // the whole runtime (blocked, adapter unusable); a renderer loss is a
        // page failure the user can recover from.
        if (pending)
            operations().settleOperation(identity, attempt,
                kind === "browser" ? "blocked" : "failed",
                "process-lost-" + kind, "renderer-process-failed", {})
        sessions.advanceBrowserDocument(sessionId)
        clearClose()
        clearExternal()
    }
    function documentReplaced() {
        // A successful user-facing replacement document retires external
        // unload state; consent for that navigation is not space permission.
        if (externalLeaveActive) clearExternal()
    }
    Timer {
        id: departureDeadline
        interval: controller.departureDeadlineMs
        onTriggered: controller.departBlocked()
    }
    Connections {
        target: controller.browser
        function onCloseConsentRequested(token, uri) {
            if (!controller.pending || controller.consent) {
                // No captured operation wants this dialog: deny, never invent.
                controller.browser.respondCloseRequest(token, false)
                return
            }
            var opened = controller.operations().openRequest(controller.attempt,
                controller.identity, "app-close", {origin: controller.originLabel(uri)})
            if (!opened.opened) {
                controller.browser.respondCloseRequest(token, false)
                controller.operations().settleOperation(controller.identity, controller.attempt,
                    "invalidated", "request-not-opened", "", {})
                controller.clearClose()
                return
            }
            controller.nativeToken = token
            controller.requestId = opened.requestId
            controller.consent = {identity: Object.assign({}, controller.identity),
                attempt: controller.attempt, kind: "close",
                origin: controller.originLabel(uri)}
            controller.changed()
        }
        function onCloseStayRecorded(token) {
            if (!controller.pending || token !== controller.nativeToken) return
            // The retained deferral completed without Accept: an explicit Stay
            // exists for this exact captured operation, independent of any
            // navigation id. Same-document retry is immediately admissible.
            var key = controller.identity, op = controller.attempt
            var resumed = controller.competingNavigation
            controller.clearClose()
            controller.competingNavigation = false
            if (resumed) {
                // The Stay belonged to a superseded close intent: record the
                // invalidation, then let the deck's pending navigation run.
                controller.operations().settleOperation(key, op, "invalidated",
                    "competing-navigation", "", {})
                controller.cancellationSettled()
                return
            }
            controller.operations().settleOperation(key, op, "cancelled", "user-stay",
                "explicit-stay-deferral-completed", {})
        }
        function onCloseDepartureVerified(operationId, nativeNavigationId) {
            if (!controller.pending || operationId !== controller.attempt) return
            var key = controller.identity, op = controller.attempt
            var descriptor = controller.capturedDescriptor
            controller.engineClosed = true
            controller.clearClose()
            var settled = controller.operations().settleOperation(key, op, "succeeded",
                "leave-departed", "correlated-navigation-departure",
                {descriptor: descriptor, nativeNavigationId: nativeNavigationId})
            if (!settled.settled) {
                // The seam refused the terminal proof; nothing was destroyed.
                controller.engineClosed = false
                controller.failure = "The close could not be verified safely. The space is unchanged."
                controller.changed()
            }
        }
        function onCloseInvalidated(reason) {
            if (!controller.pending) return
            var key = controller.identity, op = controller.attempt
            controller.clearClose()
            controller.operations().settleOperation(key, op, "invalidated", reason, "", {})
            if (reason !== "competing-navigation")
                controller.failure = "This page's close attempt was cancelled (" + reason + "). The space is unchanged."
            controller.cancellationSettled()
            controller.changed()
        }
        function onUnloadConsentRequested(token, uri) {
            if (controller.busy) {
                controller.browser.respondUnloadRequest(token, false)
                return
            }
            var key = controller.sessions.browserIdentity(controller.sessionId)
            if (!key.spaceId) { controller.browser.respondUnloadRequest(token, false); return }
            var opened = controller.operations().openRequest("", key, "external-unload",
                {origin: controller.originLabel(uri)})
            if (!opened.opened) { controller.browser.respondUnloadRequest(token, false); return }
            controller.nativeToken = token
            controller.consent = {identity: key, attempt: opened.requestId, kind: "unload",
                origin: controller.originLabel(uri)}
            controller.changed()
        }
        function onUnloadSettled(token, leave) {
            if (token !== controller.nativeToken) return
            controller.nativeToken = ""
            if (leave) controller.externalLeaveActive = true
            controller.changed()
        }
        function onRendererLost(kind) { controller.rendererLost(kind) }
        function onCompleted(location, success) {
            if (success) controller.documentReplaced()
        }
    }
    Component.onDestruction: {
        // Teardown with work in flight: settle nothing as success, only drain.
        if (pending) {
            operations().settleOperation(identity, attempt, "invalidated", "teardown", "", {})
            if (browser) browser.cancelCloseOperation()
        }
    }
}
