// Lifecycle handling adapted from Qt Lifecycle Browser; see NanoBrowser-LICENSE.txt.
// Copyright (C) 2026 The Qt Company Ltd.
import QtQuick
import QtQuick.Window
import QtWebEngine
import "FeriaPlayback.js" as Playback
import "FeriaSession.js" as Session
import "FeriaAppMode.js" as AppMode
import "FeriaJavaScript.js" as JavaScript

WebEngineView {
    id: browser
    objectName: "feriaQtWebEngine"
    required property url sourceUrl
    required property string profilePath
    property var sharedProfile: null
    // Native lifecycle owner: admits and redeems this view's asynchronous
    // work, and decides when the view has drained and may be destroyed.
    readonly property var runtime: FeriaBrowserPolicy.runtime
    // Canonical ownership for shared navigation fencing; optional so
    // standalone harnesses keep driving the raw wrapper.
    property var sessions: null
    property var pendingNavOps: ({})
    property var navOpAdmittedAt: ({})
    property int navigationDeadlineMs: 45000
    function operations() { return sessions ? sessions.browserOperations() : null }
    function admitNavigation(kind) {
        var ops = operations()
        if (!ops || !accountSessionId.length) return null
        var identity = sessions.browserIdentity(accountSessionId)
        if (!identity.spaceId) return null
        var admitted = ops.beginOperation(identity, "qtwebengine", kind)
        if (!admitted.admitted) return null
        // Dispatching a new navigation supersedes earlier in-flight attempts
        // on this engine; their admitted operations settle once now rather
        // than waiting for trailing abort callbacks.
        var ids = Object.keys(pendingNavOps)
        for (var i = 0; i < ids.length; ++i)
            settleNavOperation(ids[i], "cancelled", "superseded-load",
                "superseded-by-navigation", {})
        var updated = Object.assign({}, pendingNavOps)
        updated[admitted.operationId] = {operationId: admitted.operationId,
            kind: kind, identity: identity}
        pendingNavOps = updated
        var times = Object.assign({}, navOpAdmittedAt)
        times[admitted.operationId] = Date.now()
        navOpAdmittedAt = times
        navDeadline.restart()
        return admitted.operationId
    }
    function settleNavOperation(operationId, outcome, reason, evidence, extra) {
        if (!operationId || !pendingNavOps[operationId]) return null
        var record = pendingNavOps[operationId]
        var ops = Object.assign({}, pendingNavOps)
        delete ops[operationId]
        pendingNavOps = ops
        var times = Object.assign({}, navOpAdmittedAt)
        delete times[operationId]
        navOpAdmittedAt = times
        var result = operations().settleOperation(record.identity, operationId,
            outcome, reason, evidence, extra || {})
        if (Object.keys(pendingNavOps).length === 0) navDeadline.stop()
        return result
    }
    function settleNewestNavigation(outcome, reason, evidence) {
        var ids = Object.keys(pendingNavOps)
        if (ids.length === 0) return null
        return settleNavOperation(ids[ids.length - 1], outcome, reason, evidence, {})
    }
    Timer {
        id: navDeadline
        interval: 1000; repeat: true
        onTriggered: {
            var now = Date.now()
            var ids = Object.keys(browser.pendingNavOps)
            for (var i = 0; i < ids.length; ++i)
                if (now - browser.navOpAdmittedAt[ids[i]] > browser.navigationDeadlineMs)
                    browser.settleNavOperation(ids[i], "blocked",
                        "navigation-terminal-not-observed", "deadline-elapsed-no-terminal", {})
        }
    }
    onUrlChanged: if (!closingSession && !adoptingWindowRequest && !clearingSession)
        admitNavigation("navigate")
    readonly property alias pageTools: pageToolsController
    FeriaPageTools { id: pageToolsController; browser: browser }
    readonly property alias browserRequests: requestsController
    signal message(string text)
    signal tabRequested(var request)
    property bool managedBeforeUnload: false
    property var windowAdmission: null
    property int windowAdmissionSerial: 0
    signal closeRejected()
    // QtWebEngine 6.11.1 qquickwebengineview.cpp::windowCloseRejected invokes
    // this method dynamically. This is an UNDOCUMENTED PRIVATE compatibility
    // dependency, not a public Qt signal. Replace with a public acknowledgement
    // once available; the controller fails safely if this hook is not delivered.
    function windowCloseRejected() { closeRejected() }
    FeriaBrowserRequests {
        id: requestsController
        browser: browser
        onMessage: (text) => browser.message(text)
    }
    // Adapted from Qt's BSD-licensed Lifecycle Browser. Only freeze when the
    // engine says it is safe; audible pages and pending callbacks stay active.
    property bool freezeBackground: true
    property int freezeDelay: 30000
    property bool lifecycleBlocked: false
    property bool activitySuspended: false
    property int presentationSerial: 0
    function applyPresentationActivity() {
        if (closingSession || rendererLost || replacementLoadPending || !documentReady) return
        wakePage()
        evaluateJavaScript(Playback.presentationMedia(activitySuspended, navigationGeneration, presentationSerial))
    }
    onActivitySuspendedChanged: {
        var serial = ++presentationSerial
        // Coalesce rapid world/minimize changes before dispatching page commands.
        Qt.callLater(function() {
            if (serial === browser.presentationSerial) browser.applyPresentationActivity()
        })
    }
    onLifecycleBlockedChanged: if (lifecycleBlocked) wakePage()
    function wakePage() {
        // Eligibility owns the timer. Stopping it here while already Active
        // leaves an eligible hidden view stopped until a dependency changes.
        if (lifecycleState !== WebEngineView.LifecycleState.Active)
            lifecycleState = WebEngineView.LifecycleState.Active
    }
    onVisibleChanged: if (visible) wakePage()
    onFreezeBackgroundChanged: if (!freezeBackground) wakePage()
    Timer {
        id: freezeTimer
        interval: browser.freezeDelay
        running: browser.freezeBackground && !browser.visible && !browser.closingSession
            && !browser.lifecycleBlocked && !browser.rendererLost && !browser.adoptingWindowRequest
            && !browser.recentlyAudible
            && browser.recommendedState !== WebEngineView.LifecycleState.Active
            && browser.lifecycleState === WebEngineView.LifecycleState.Active
        onTriggered: {
            if (browser.pendingJavaScript === 0 && !browser.lifecycleBlocked)
                browser.lifecycleState = WebEngineView.LifecycleState.Frozen
            else restart()
        }
    }
    // Drain callbacks before the owner destroys this view. In Qt 6.11.1 a
    // pending JavaScript callback can reach a torn-down QML context on close.
    property bool closingSession: false
    property bool adoptingWindowRequest: false
    property var javascriptTickets: ({})
    property int javascriptTicketSerial: 0
    readonly property int pendingJavaScript: Object.keys(javascriptTickets).length
    property int javascriptEpoch: 0
    // Every document boundary this adapter observes moves the native epoch
    // that script and prompt tickets are bound to.
    onJavascriptEpochChanged: runtime.documentChanged(browser)
    property bool destructionNotified: false
    signal safeToDestroy()
    function notifySafeToDestroy() {
        if (!closingSession || pendingJavaScript !== 0 || destructionNotified
            || !runtime.drained(browser)) return
        destructionNotified = true
        stop()
        safeToDestroy()
    }
    function invalidateJavaScriptTickets() {
        for (var id of Object.keys(javascriptTickets)) JavaScript.invalidate(javascriptTickets[id])
        javascriptTickets = ({})
        // The engine will never answer these; the native ledger drops them too.
        if (runtime) runtime.retireWork(browser)
    }
    function finishJavaScript(ticket, result, callback) {
        if (javascriptTickets[ticket.id] !== ticket) return
        var remaining = Object.assign({}, javascriptTickets)
        delete remaining[ticket.id]
        javascriptTickets = remaining
        // The native ticket is redeemed exactly once. It is refused when the
        // document, engine generation, page or account it was admitted for
        // is no longer current, so a late result cannot act on a replacement.
        var live = runtime.redeemWork(ticket.work)
        try {
            if (live && !closingSession && !rendererLost
                    && typeof callback === "function") callback(result)
        } finally {
            notifySafeToDestroy()
        }
    }
    function evaluateJavaScript(script, callback) {
        if (closingSession || adoptingWindowRequest || rendererLost) return false
        if (lifecycleState !== WebEngineView.LifecycleState.Active) return false
        var work = runtime.admitWork(browser, "script")
        if (!work) return false
        var ticket = JavaScript.ticket(browser, ++javascriptTicketSerial, work, callback)
        var pending = Object.assign({}, javascriptTickets)
        pending[ticket.id] = ticket
        javascriptTickets = pending
        runJavaScript(script, JavaScript.resultCallback(ticket))
        return true
    }
    function prepareForClose(engineTerminal) {
        if (closingSession) return
        wakePage()
        // Pause without depending on a provider-specific connector, then drain
        // callbacks before stopping the engine or destroying its QML context.
        // No fresh IPC after an observed engine close terminal: its document
        // has unloaded and may never answer a newly dispatched script.
        if (!engineTerminal)
            evaluateJavaScript("document.querySelectorAll('video,audio').forEach(function(m){m.pause()})")
        closingSession = true
        runtime.beginRetirement(browser)
        // Release focus while the retained owner's scope is still alive,
        // before deferred parent/Loader destruction reaches the native view.
        focus = false
        documentCheck.stop(); cleanupPoll.stop()
        notifySafeToDestroy()
    }
    // Application exit only: there is no event loop left to drain into. With
    // Qt 6.11.1, deleting a view that still owed script callbacks faulted
    // intermittently (access violation in Qt6Qml, called from
    // Qt6WebEngineQuick while Qt6WebEngineCore tore the web contents down
    // inside the view's destructor). Discarding destroys the web contents
    // now, while this object is intact, so whatever the engine delivers on
    // teardown arrives before the destructor. Measured, not proven: see
    // docs/feria-browser-foundation-set-1.md.
    function discardForExit() {
        invalidateJavaScriptTickets()
        closingSession = true
        documentCheck.stop(); cleanupPoll.stop(); freezeTimer.stop()
        focus = false
        visible = false
        lifecycleState = WebEngineView.LifecycleState.Discarded
        return lifecycleState === WebEngineView.LifecycleState.Discarded
    }
    property string providerId: ""
    property string accountVisitToken: ""
    property string accountSessionId: ""
    property bool fullScreenActive: false
    Shortcut {
        sequence:"Escape"
        context:Qt.WindowShortcut
        enabled:browser.visible && browser.fullScreenActive && !pageToolsController.findOpen
        onActivated:browser.evaluateJavaScript("if(document.fullscreenElement) document.exitFullscreen()")
    }
    property bool appLayout: true
    property bool appKeyboard: true
    property string appAccent: "#f2c94c"
    function applyAppMode() {
        if (providerId.length > 0 && documentReady && !clearingSession)
            // Navigation can replace the document after this IPC is queued.
            // Guard DOM availability at execution, not only at QML dispatch.
            evaluateJavaScript("(()=>{if(!document.documentElement||!document.head)return;"
                + AppMode.script(providerId,appLayout,appKeyboard,appAccent) + "})()")
    }
    onAppLayoutChanged: applyAppMode()
    onAppKeyboardChanged: applyAppMode()
    onProviderIdChanged: applyAppMode()
    Timer {
        interval:1500; repeat:true; running:!browser.closingSession && browser.documentReady && !browser.clearingSession
            && browser.providerId.length > 0 && browser.lifecycleState === WebEngineView.LifecycleState.Active
        onTriggered:browser.applyAppMode()
    }
    signal playbackObserved(var observation)
    signal resumeUnavailable()
    property real resumePosition: 0
    property var resumeLocator: null
    property string observationMode: "watch"
    property bool observationEnabled: true
    property int readingResumeAttempts: 0
    signal captureProtectionChanged(bool blocked, string visitToken)
    function cancelMediaResume(freshStart) { mediaGate.cancelResume(freshStart) }
    function samplePlayback() { mediaGate.poll() }
    function receiveMediaResult(label, frame) { mediaGate.acceptResult(label, frame) }
    FeriaMediaSession {
        id: mediaGate
        providerId: browser.providerId; sourceUrl: String(browser.sourceUrl)
        mode: browser.observationMode; initialPosition: browser.resumePosition
        onScriptRequested: (label, script) => browser.evaluateJavaScript(script, function(result) {
            browser.receiveMediaResult(label, result)
        })
        onProtectionChanged: (blocked) => browser.captureProtectionChanged(blocked, browser.accountVisitToken)
        onUnavailable: browser.resumeUnavailable()
        onConnectorFailed: (reason) => console.warn("Feria connector:", reason)
        onRestored: browser.resumePosition = 0
        onObservation: (sample) => {
            if (browser.resumeLocator && browser.resumeLocator.type === "url") browser.resumeLocator = null
            if (browser.resumeLocator && sample.kind === "book"
                    && Playback.sameDestination(sample.href, browser.sourceUrl)) {
                ++browser.readingResumeAttempts
                var generation = browser.navigationGeneration
                browser.evaluateJavaScript(Playback.restoreReading(browser.resumeLocator), function(ok) {
                    if (generation !== browser.navigationGeneration) return
                    if (ok) browser.resumeLocator = null
                    else if (browser.readingResumeAttempts >=
                             (browser.resumeLocator && browser.resumeLocator.type === "mangaplus-page" ? 60 : 10)) {
                        browser.resumeLocator = null; browser.resumeUnavailable()
                    }
                })
                return
            }
            if (browser.observationEnabled) {
                if (browser.accountVisitToken) sample.visitToken = browser.accountVisitToken
                browser.playbackObserved(sample)
            }
        }
    }
    onNavigationGenerationChanged: if (mediaGate && mediaGate.initialized) mediaGate.navigationChanged()
    Timer {
        interval: 2000; repeat: true
        running: !browser.closingSession && !browser.rendererLost && browser.lifecycleState === WebEngineView.LifecycleState.Active
            && (browser.observationEnabled || browser.resumePosition > 0 || browser.resumeLocator !== null)
        onTriggered: browser.samplePlayback()
    }
    signal localSessionCleared(bool success)
    property bool clearingSession: false
    property var clearOrigins: []
    property var clearDomains: []
    property bool cleanupNavigationPending: false
    property bool cleanupComplete: true
    function clearLocalSession(domains, origins) {
        cleanupComplete = true
        clearingSession = true; clearDomains = domains; clearOrigins = origins.slice(); clearNextOrigin()
    }
    function clearNextOrigin() {
        if (clearOrigins.length === 0) {
            clearingSession = false; localSessionCleared(cleanupComplete)
            return
        }
        var origin = clearOrigins[0]; clearOrigins = clearOrigins.slice(1)
        // This blank page has the site's origin but executes no provider scripts
        // and makes no network request. It can clear origin storage offline.
        cleanupNavigationPending = true
        loadHtml("<!doctype html><title>Feria session cleanup</title>", origin + "/")
    }
    function acceptCleanupResult(result) {
        if (result === "done" || result === "partial") {
            cleanupPoll.stop()
            if (result === "partial") cleanupComplete = false
            clearNextOrigin()
        } else if (result === "failed") {
            cleanupPoll.stop(); clearingSession = false; localSessionCleared(false)
        }
    }
    Timer {
        id: cleanupPoll; interval: 100; repeat: true
        onTriggered: browser.evaluateJavaScript("window.__feriaStorageCleared || ''", browser.acceptCleanupResult)
    }
    property bool suppressed: false
    readonly property bool ready: true
    property var popups: []
    property bool documentReady: false
    property int navigationGeneration: 0
    property bool replacementLoadPending: false
    property bool rendererLost: false
    signal documentReplaced(int generation)
    signal mainDocumentLoaded(string location)
    signal documentLoadUnsuccessful(int generation, string location)
    // Engine load state for the shell; WebEngineView reports real loadProgress.
    readonly property bool loadActive: loading
    property bool stopRequested: false
    function stopLoad() {
        if (!loading) return
        admitNavigation("stop")
        stopRequested = true
        stop()
    }
    readonly property alias navigation: navigationController
    FeriaNavigation { id: navigationController; browser: browser }
    readonly property bool navCanGoBack: navigationController.canGoBack
    readonly property bool navCanGoForward: navigationController.canGoForward
    function pageBack() { return navigationController.back() }
    function pageForward() { return navigationController.forward() }
    signal started(string location)
    // Emitted before a stopped load's completed(location,false): the page
    // remains usable, so the shell must not raise a load-failure error.
    signal stopped(string location)
    signal completed(string location, bool success)
    signal failed(string reason)
    signal exitRequested()
    signal optionsRequested()
    Shortcut {
        sequences:["F10","Menu"]
        context:Qt.WindowShortcut
        enabled:browser.visible && !browser.fullScreenActive
        onActivated:browser.optionsRequested()
    }
    url: sourceUrl
    backgroundColor: "#08090d"
    settings.javascriptCanOpenWindows: true
    settings.playbackRequiresUserGesture: false
    settings.fullScreenSupportEnabled: true
    // QtWebEngine's built-in PDF viewer requires plug-ins for inline navigation.
    settings.pdfViewerEnabled: true
    settings.pluginsEnabled: true
    profile: sharedProfile || localProfile
    WebEngineProfile {
        id: localProfile
        // The compatibility/cleaner path still owns its private persistent
        // profile. Retained views use the shared profile and leave this unused
        // fallback off-the-record, without another cookie database handle.
        storageName: browser.sharedProfile ? "" : "feria"
        offTheRecord: browser.sharedProfile !== null
        persistentStoragePath: browser.sharedProfile ? "" : browser.profilePath + "/qtwebengine"
        cachePath: browser.sharedProfile ? "" : browser.profilePath + "/qtwebengine-cache"
        persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
    }
    onNavigationRequested: function(request) {
        // loadHtml creates a data URL internally. Permit just the next blank
        // document initiated by cleanup; normal pages retain the URL policy.
        if (clearingSession && cleanupNavigationPending && String(request.url).indexOf("data:text/html") === 0) {
            cleanupNavigationPending = false
            return
        }
        if (!FeriaBrowserPolicy.allowsNavigation(request.url)) request.reject()
    }
    onLoadingChanged: function(info) {
        if (closingSession) return
        if (adoptingWindowRequest) {
            // Qt initialization reports the original adopted content, including
            // a silent about:blank popup. Preserve its adoption observation;
            // this is not a reconstructed main-document load or recovery.
            if (!rendererLost && info.status === WebEngineView.LoadSucceededStatus)
                completed(String(info.url), true)
            return
        }
        if (info.status === WebEngineView.LoadStartedStatus) {
            fullScreenActive = false
            stopRequested = false
            ++navigationGeneration
            ++javascriptEpoch
            replacementLoadPending = true
            documentReady = false
            if (!rendererLost) documentCheck.restart()
            started(String(info.url))
        }
        else if (info.status === WebEngineView.LoadSucceededStatus) {
            if (!replacementLoadPending || loading || String(info.url) !== String(url)) return
            stopRequested = false
            documentCheck.stop()
            rendererLost = false
            documentReady = true
            applyAppMode()
            if (clearingSession) {
                evaluateJavaScript(Session.clearStorage)
                cleanupPoll.start()
            }
            // Qt's DidStartNavigation excludes same-document navigation. Only
            // its successful main-document completion releases old barriers.
            replacementLoadPending = false
            settleNewestNavigation("succeeded", "native-history", "navigation-completed")
            documentReplaced(navigationGeneration)
            mainDocumentLoaded(String(info.url))
            applyPresentationActivity()
            // A completed listener may begin cleanup synchronously. Inspect the
            // current load before notifying it, so about:blank is never cleaned.
            completed(String(info.url), true)
        } else if (info.status === WebEngineView.LoadStoppedStatus) {
            // stop() ends the load as "stopped" (errorCode -3 observed), not as
            // a failure: the previous page — or the initial blank document —
            // remains usable, which is not an open failure. A stopped callback
            // that trails a newer load is superseded and must not overwrite the
            // replacing load's state.
            if (!stopRequested && loading) return
            replacementLoadPending = false
            settleNewestNavigation("cancelled", "load-stopped", "user-stop-requested")
            stopRequested = false
            ++navigationGeneration
            ++javascriptEpoch
            documentCheck.stop()
            documentReady = false
            if (clearingSession) { cleanupPoll.stop(); clearingSession = false; localSessionCleared(false) }
            documentLoadUnsuccessful(navigationGeneration, String(info.url))
            // A stopped load is a cancellation, never a successful
            // intended-document load: report false with a distinct stopped
            // signal so the shell keeps the usable-page UX.
            stopped(String(info.url))
            completed(String(info.url), false)
        } else if (info.status === WebEngineView.LoadFailedStatus) {
            // An older aborted request can trail a newer main-frame start.
            // Preserve that newer load's gate, without treating the abort as success.
            if (!loading || (info.errorCode !== 3 && info.errorCode !== -3))
                replacementLoadPending = false
            ++navigationGeneration
            ++javascriptEpoch
            documentCheck.stop()
            documentReady = false
            if (clearingSession) { cleanupPoll.stop(); clearingSession = false; localSessionCleared(false) }
            documentLoadUnsuccessful(navigationGeneration, String(info.url))
            settleNewestNavigation(info.errorCode === 3 || info.errorCode === -3
                ? "cancelled" : "failed", "load-unsuccessful",
                info.errorCode === 3 || info.errorCode === -3
                    ? "superseded-by-navigation" : "navigation-failed")
            if (info.errorCode === 3 || info.errorCode === -3) {
                // ERR_ABORTED: a superseded navigation or a deliberate stop.
                // A superseded load must not overwrite the state of the load
                // that replaced it, and a stop leaves the previous page usable
                // — neither is an open failure.
                if (stopRequested) { stopRequested = false; completed(String(info.url), true) }
                return
            }
            completed(String(info.url), false)
        }
    }
    Timer {
        id: documentCheck
        interval: 500
        repeat: true
        onTriggered: {
            var generation = browser.navigationGeneration
            browser.evaluateJavaScript("({ready:document.readyState !== 'loading' && !!document.body && document.body.childElementCount > 0,url:location.href})", function(result) {
                if (browser.rendererLost || browser.closingSession || generation !== browser.navigationGeneration || !result || result.ready !== true
                        || String(result.url) !== String(browser.url)) return
                documentCheck.stop()
                browser.documentReady = true
                browser.applyAppMode()
            })
        }
    }
    onRenderProcessTerminated: function(status, exitCode) {
        // A terminal can arrive while live-close callbacks are still draining.
        // Those requests can no longer answer; retire them and release once.
        if (closingSession) {
            invalidateJavaScriptTickets()
            ++javascriptEpoch
            documentCheck.stop(); cleanupPoll.stop(); freezeTimer.stop()
            notifySafeToDestroy()
            return
        }
        // Routine renderer retirement stays quiet. The close controller still
        // receives the original engine signal independently.
        if (status === WebEngineView.NormalTerminationStatus) return
        // Generation changes synchronously request media invalidation scripts.
        // Latch first so none can be admitted to a renderer that has terminated.
        rendererLost = true
        // Qt Quick can retain unanswered native callbacks until adapter disposal.
        // Retire this terminal's tickets, never live-close or new-renderer work.
        invalidateJavaScriptTickets()
        // Same activity, new engine generation: nothing admitted for the dead
        // renderer can be redeemed against the one that replaces it.
        runtime.rendererLost(browser)
        ++navigationGeneration
        ++javascriptEpoch
        ++presentationSerial
        documentCheck.stop()
        cleanupPoll.stop()
        freezeTimer.stop()
        replacementLoadPending = false
        documentReady = false
        if (clearingSession) { clearingSession = false; localSessionCleared(false) }
        message("The page renderer is unavailable. Reload to open a new document; unsaved page state may be lost.")
        completed(String(url), false)
    }
    onFullScreenRequested: function(request) { request.accept(); fullScreenActive = request.toggleOn }
    onNewWindowRequested: function(request) {
        if (!request.userInitiated) { message("Popup blocked"); return }
        // Retained spaces use the deck's canonical same-profile destination and
        // disposable native host, including dialogs. Legacy cleaner views keep
        // their separate compatibility popup path.
        if (sharedProfile) {
            // Synchronous one-shot capability: no Qt request object survives
            // dispatch, and observers cannot replay a handled request afterward.
            windowAdmission = {request: request, profile: profile, sessionId: accountSessionId,
                documentGeneration: navigationGeneration, serial: ++windowAdmissionSerial, consumed: false}
            try { tabRequested(request) }
            finally { windowAdmission = null }
        } else openPopup(request)
    }
    function openPopup(request) {
        if (!FeriaBrowserPolicy.allowsNavigation(String(request.requestedUrl || "about:blank"))) return
        var popup = popupComponent.createObject(browser, { "sharedProfile": browser.profile })
        if (!popup) return
        popups = popups.concat([popup])
        request.openIn(popup.view)
        if (request.requestedGeometry.width > 0 && request.requestedGeometry.height > 0) {
            popup.width = Math.max(320, Math.min(1600, request.requestedGeometry.width))
            popup.height = Math.max(240, Math.min(1000, request.requestedGeometry.height))
        }
        popup.show()
    }
    onSuppressedChanged: {
        if (suppressed) {
            var windows = popups.slice()
            for (var i = 0; i < windows.length; ++i) windows[i].close()
        }
    }
    Component.onCompleted: {
        FeriaBrowserPolicy.registerRuntimeView(browser)
        FeriaBrowserPolicy.registerRuntimeProfile(profile)
    }
    Component.onDestruction: {
        invalidateJavaScriptTickets()
        var ids = Object.keys(pendingNavOps)
        for (var i = 0; i < ids.length; ++i)
            settleNavOperation(ids[i], "invalidated", "teardown", "", {})
        var windows = popups.slice()
        for (var j = 0; j < windows.length; ++j) windows[j].destroy()
    }
    Component {
        id: popupComponent
        Window {
            id: popup
            title: popupView.title || "Feria"
            width: 900
            height: 700
            required property var sharedProfile
            readonly property alias view: popupView
            onClosing: {
                browser.popups = browser.popups.filter(function(item) { return item !== popup })
                destroy()
            }
            WebEngineView {
                id: popupView
                anchors.fill: parent
                profile: popup.sharedProfile
                readonly property string accountSessionId: browser.accountSessionId
                readonly property string accountVisitToken: browser.accountVisitToken
                Component.onCompleted: FeriaBrowserPolicy.registerRuntimeView(popupView)
                settings.javascriptCanOpenWindows: true
                onNavigationRequested: function(request) {
                    if (!FeriaBrowserPolicy.allowsNavigation(request.url)) request.reject()
                }
                onNewWindowRequested: function(request) {
                    if (request.userInitiated) browser.openPopup(request)
                }
                FeriaBrowserRequests {
                    browser: popupView
                    onMessage: (text) => browser.message(text)
                }
                onWindowCloseRequested: popup.close()
            }
        }
    }
}

