import QtQuick
import Colosseum.FeriaHost 1.0
import "FeriaPlayback.js" as Playback
import "FeriaAppMode.js" as AppMode

FeriaHostItem {
    id: browser
    objectName: "feriaWebView2"
    // Defaults (not required) so decks can create this view through a
    // URL-loaded Loader in harnesses where the native module is registered
    // only when the WebView2 engine is actually selected.
    property url sourceUrl: ""
    property string profilePath: ""
    property string providerId: ""
    property string accountVisitToken: ""
    // Canonical ownership for operation fencing; optional so standalone
    // harnesses can still drive the raw native wrapper.
    property var sessions: null
    property string accountSessionId: ""
    property bool closingSession: false
    // Admitted-but-unsettled navigation operations, keyed by operationId,
    // plus the native attempt correlation. Every admitted operation settles
    // exactly once: superseded and cancelled attempts included.
    property var pendingNavOps: ({})
    property var loadOpByNavId: ({})
    property var navOpAdmittedAt: ({})
    property var lastStartedNavId: 0
    property bool lastCompletionCancelled: false
    property bool stopDispatched: false
    property bool traversalNavigation: false
    // Bounded settlement for a native attempt that never reaches a terminal
    // (the observed suppressed-dialog stall): blocked, never success.
    property int navigationDeadlineMs: 45000
    // Deck-facing normalization of the same names Qt's wrapper exposes.
    readonly property bool loading: loadActive
    readonly property bool documentReady: appDocumentReady && !loadActive
    function wakePage() {
        // WebView2 keeps pages active; there is no frozen lifecycle to exit.
    }
    property bool destructionNotified: false
    signal safeToDestroy()
    function notifySafeToDestroy() {
        if (!closingSession || destructionNotified) return
        destructionNotified = true
        Qt.callLater(function() { if (browser) browser.safeToDestroy() })
    }
    Component.onCompleted: {
        // The runtime view must be registered before deck quiescence can
        // release engine profiles: the native host outlives QML teardown.
        if (typeof FeriaBrowserPolicy !== "undefined")
            FeriaBrowserPolicy.registerRuntimeView(browser)
    }
    Component.onDestruction: {
        // Every still-admitted operation settles once; teardown is never a
        // native terminal for any of them.
        var ids = Object.keys(pendingNavOps)
        for (var i = 0; i < ids.length; ++i)
            settleNavOperation(ids[i], "invalidated", "teardown", "", {})
    }
    function prepareForClose(engineTerminal) {
        if (closingSession) return
        if (!engineTerminal) executeScript("close-pause",
            "document.querySelectorAll('video,audio').forEach(function(m){m.pause()})")
        closingSession = true
        // WebView2 has no queued JavaScript tickets to drain here; retained
        // dialog deferrals drain in the native host teardown. Release the
        // retained owner's scope after this turn of the event loop.
        notifySafeToDestroy()
    }
    function operations() { return sessions ? sessions.browserOperations() : null }
    function admitNavigation(kind) {
        var ops = operations()
        if (!ops || !accountSessionId.length) return null
        var identity = sessions.browserIdentity(accountSessionId)
        if (!identity.spaceId) return null
        var admitted = ops.beginOperation(identity, "webview2", kind)
        if (!admitted.admitted) return null
        var record = {operationId: admitted.operationId, kind: kind,
            identity: identity, navId: 0}
        var updated = Object.assign({}, pendingNavOps)
        updated[admitted.operationId] = record
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
        if (record.navId > 0) {
            var byNav = Object.assign({}, loadOpByNavId)
            delete byNav[record.navId]
            loadOpByNavId = byNav
        }
        var result = operations().settleOperation(record.identity, operationId,
            outcome, reason, evidence, extra || {})
        if (Object.keys(pendingNavOps).length === 0) navDeadline.stop()
        return result
    }
    function settleNavByNavId(navId, outcome, reason, evidence) {
        var operationId = loadOpByNavId[navId]
        if (!operationId) return null
        return settleNavOperation(operationId, outcome, reason, evidence, {})
    }
    function newestUnstartedLoadOp() {
        var ops = operations()
        if (!ops) return ""
        var pending = ops.pendingOperation(accountSessionId)
        return pending && pendingNavOps[pending.operationId]
            && pendingNavOps[pending.operationId].navId === 0 ? pending.operationId : ""
    }
    function settleUnstartedLoads(outcome, reason, evidence) {
        var ids = Object.keys(pendingNavOps)
        for (var i = 0; i < ids.length; ++i) {
            var record = pendingNavOps[ids[i]]
            if (record.navId === 0)
                settleNavOperation(ids[i], outcome, reason, evidence, {})
        }
    }
    Timer {
        id: navDeadline
        interval: 1000; repeat: true
        triggeredOnStart: false
        onTriggered: {
            var now = Date.now()
            var ids = Object.keys(browser.pendingNavOps)
            var expired = []
            for (var i = 0; i < ids.length; ++i)
                if (now - browser.navOpAdmittedAt[ids[i]] > browser.navigationDeadlineMs)
                    expired.push(ids[i])
            for (var j = 0; j < expired.length; ++j)
                browser.settleNavOperation(expired[j], "blocked",
                    "navigation-terminal-not-observed", "deadline-elapsed-no-terminal", {})
        }
    }
    onUrlChanged: if (!closingSession && !traversalNavigation) admitNavigation("navigate")
    property bool fullScreenActive: false
    onFullScreenChanged: function(active) { fullScreenActive = active }
    property bool appLayout: true
    property bool appKeyboard: true
    property string appAccent: "#f2c94c"
    property bool appDocumentReady: false
    function applyAppMode() {
        if (providerId.length > 0 && ready && appDocumentReady && !clearingSession) executeScript("feria-app-mode",AppMode.script(providerId,appLayout,appKeyboard,appAccent))
    }
    onAppLayoutChanged: applyAppMode()
    onAppKeyboardChanged: applyAppMode()
    onProviderIdChanged: applyAppMode()
    Timer {
        interval:1500; repeat:true; running:browser.ready && browser.appDocumentReady && !browser.clearingSession && !browser.closingSession && browser.providerId.length > 0
        onTriggered:browser.applyAppMode()
    }
    signal playbackObserved(var observation)
    signal resumeUnavailable()
    property real resumePosition: 0
    property var resumeLocator: null
    property string observationMode: "watch"
    property bool observationEnabled: true
    property int readingResumeAttempts: 0
    property int navigationGeneration: 0
    signal captureProtectionChanged(bool blocked, string visitToken)
    function cancelMediaResume(freshStart) { mediaGate.cancelResume(freshStart) }
    function samplePlayback() { mediaGate.poll() }
    function receiveMediaResult(label, frame) { mediaGate.acceptResult(label, frame) }
    FeriaMediaSession {
        id: mediaGate
        providerId: browser.providerId; sourceUrl: String(browser.sourceUrl)
        mode: browser.observationMode; initialPosition: browser.resumePosition
        onScriptRequested: (label, script) => browser.executeScript(label, script)
        onProtectionChanged: (blocked) => browser.captureProtectionChanged(blocked, browser.accountVisitToken)
        onUnavailable: browser.resumeUnavailable()
        onConnectorFailed: (reason) => console.warn("Feria connector:", reason)
        onRestored: browser.resumePosition = 0
        onObservation: (sample) => {
            if (browser.resumeLocator && browser.resumeLocator.type === "url") browser.resumeLocator = null
            if (browser.resumeLocator && sample.kind === "book"
                    && Playback.sameDestination(sample.href, browser.sourceUrl)) {
                ++browser.readingResumeAttempts
                browser.executeScript("feria-reading-resume-" + browser.navigationGeneration,
                    Playback.restoreReading(browser.resumeLocator))
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
        running: browser.ready && !browser.closingSession && (browser.observationEnabled || browser.resumePosition > 0 || browser.resumeLocator !== null)
        onTriggered: browser.samplePlayback()
    }
    onScriptResult: function(label, json) {
        if (label === "feria-reading-resume-" + navigationGeneration) {
            if (json === "true") resumeLocator = null
            else if (readingResumeAttempts >= (resumeLocator && resumeLocator.type === "mangaplus-page" ? 60 : 10)) {
                resumeLocator = null; resumeUnavailable()
            }
            return
        }
        try { browser.receiveMediaResult(label, JSON.parse(json)) }
        catch (e) { browser.receiveMediaResult(label, {outcome:"error",reason:"invalid-script-result"}) }
    }
    signal localSessionCleared(bool success)
    property var clearOrigins: []
    property var clearDomains: []
    property bool clearingSession: false
    function clearLocalSession(domains, origins) {
        clearingSession = true; clearDomains = domains; clearOrigins = origins.slice(); clearNextOrigin()
    }
    function clearNextOrigin() {
        if (clearOrigins.length === 0) { clearCookies(clearDomains); return }
        var next = clearOrigins[0]; clearOrigins = clearOrigins.slice(1); clearSiteData(next)
    }
    onSiteDataCleared: function(success) {
        if (!clearingSession) return
        if (success) clearNextOrigin()
        else { clearingSession = false; localSessionCleared(false) }
    }
    onCookiesCleared: function(success) {
        if (clearingSession) { clearingSession = false; localSessionCleared(success) }
    }
    signal started(string location)
    // Emitted before a cancelled load's completed(location,false): the page
    // remains usable, so the shell must not raise a load-failure error for it.
    signal stopped(string location)
    signal completed(string location, bool success)
    // Deck-parity lifecycle signals: WebView2's main-frame NavigationCompleted
    // is the terminal load state; same-document changes do not fire it.
    signal mainDocumentLoaded(string location)
    signal documentReplaced(int generation)
    signal documentLoadUnsuccessful(int generation, string location)
    signal failed(string reason)
    signal exitRequested()
    signal optionsRequested()
    // Engine load state for the shell. WebView2 reports no load percentage,
    // so loadProgress stays -1 and the shell shows an indeterminate indicator.
    // DOM readiness does not end a load — subresources can still be fetching —
    // so loadActive only clears on the terminal NavigationCompleted.
    property bool loadActive: false
    readonly property real loadProgress: -1
    readonly property bool navCanGoBack: canGoBack
    readonly property bool navCanGoForward: canGoForward
    function pageBack() { goBack() }
    function pageForward() { goForward() }
    function stopLoad() { if (loadActive) stop() }
    onAppMenuRequested: optionsRequested()
    // Native per-view history commands with honest outcomes. WebView2 exposes
    // no offset traversal; goToOffset-style choosers report unsupported.
    readonly property QtObject navigation: QtObject {
        readonly property bool usable: browser.ready && !browser.closingSession && !browser.clearingSession
        readonly property bool canGoBack: usable && browser.navCanGoBack
        readonly property bool canGoForward: usable && browser.navCanGoForward
        readonly property bool loading: usable && browser.loadActive
        readonly property int progress: -1
        function back() {
            if (!canGoBack) return false
            browser.traversalNavigation = true
            browser.admitNavigation("back")
            browser.wakePage(); browser.goBack(); return true
        }
        function forward() {
            if (!canGoForward) return false
            browser.traversalNavigation = true
            browser.admitNavigation("forward")
            browser.wakePage(); browser.goForward(); return true
        }
        function reload() {
            if (!usable) return false
            browser.admitNavigation("reload")
            browser.wakePage(); browser.reload(); return true
        }
        function stop() {
            if (!loading) return false
            // A deliberate user stop is explicit cancellation evidence, never
            // a timeout guess; both the stop command and the original load
            // settle when the native cancellation is observed.
            browser.admitNavigation("stop")
            browser.stopDispatched = true
            browser.stopLoad(); return true
        }
        function goToOffset(offset, revision) { return false }
    }
    url: sourceUrl
    // Never derive a user-data folder from an unassigned profile: the loader
    // assigns profile/identity bindings before the first navigation.
    userDataFolder: profilePath.length > 0 ? profilePath + "/webview2" : ""
    onNavigationStarted: function(location, navId) {
        fullScreenActive = false; appDocumentReady = false; ++navigationGeneration
        loadActive = true; traversalNavigation = false
        lastStartedNavId = navId
        // Correlate the newest admitted load attempt with this native id.
        if (navId > 0) {
            var unstarted = newestUnstartedLoadOp()
            if (unstarted) {
                var record = pendingNavOps[unstarted]
                record.navId = navId
                var updated = Object.assign({}, pendingNavOps)
                updated[unstarted] = record
                pendingNavOps = updated
                var byNav = Object.assign({}, loadOpByNavId)
                byNav[navId] = unstarted
                loadOpByNavId = byNav
            }
        }
        // Document generation is canonical state: only a genuine user-facing
        // navigation advances it (the internal close unload never reaches
        // this handler), which also retires any stale captured identity. The
        // operation settles against the identity captured at admission: the
        // seam treats a navigation's own document commit as expected.
        if (!closingSession && sessions && accountSessionId.length)
            sessions.advanceBrowserDocument(accountSessionId)
        started(location)
    }
    onLoadCancelled: function(navId, location) {
        // Announced immediately before the coerced completion: this attempt
        // is a cancellation, never a successful intended-document load. It
        // settles its own admitted load (and the stop that requested it).
        lastCompletionCancelled = true
        var evidence = stopDispatched ? "user-stop-requested" : "superseded-by-navigation"
        if (navId > 0) settleNavByNavId(navId, "cancelled", "load-cancelled", evidence)
        else settleUnstartedLoads("cancelled", "load-cancelled", evidence)
        if (stopDispatched) {
            var ids = Object.keys(pendingNavOps)
            for (var i = 0; i < ids.length; ++i)
                if (pendingNavOps[ids[i]].kind === "stop")
                    settleNavOperation(ids[i], "cancelled", "user-stop", "user-stop-requested", {})
            stopDispatched = false
        }
    }
    onNavigationSuperseded: function(navId) {
        // The replaced attempt's terminal is not a shell load outcome, but its
        // admitted operation settles exactly once as cancelled.
        if (navId > 0) settleNavByNavId(navId, "cancelled", "superseded-load",
            "superseded-by-navigation")
    }
    onDocumentReady: function(location) { appDocumentReady = true; applyAppMode(); completed(location, true) }
    onNavigationCompleted: function(location, success) {
        appDocumentReady = success; loadActive = false
        var cancelled = lastCompletionCancelled
        lastCompletionCancelled = false
        var operationId = loadOpByNavId[lastStartedNavId]
        if (cancelled) {
            // A cancelled load keeps the shell's previous page usable, but it
            // is never a successful intended-document load: completed reports
            // false, the distinct stopped signal preserves the usable-page
            // UX, and documentReplaced/mainDocumentLoaded stay suppressed so
            // restore/reopen state is not consumed.
            stopped(location)
            completed(location, false)
            return
        }
        if (success) {
            applyAppMode()
            if (operationId)
                settleNavOperation(operationId, "succeeded", "native-history",
                    "navigation-completed", {nativeNavigationId: lastStartedNavId})
        } else if (operationId) {
            settleNavOperation(operationId, "failed", "load-unsuccessful", "navigation-failed", {})
        }
        completed(location, success)
        if (closingSession) return
        if (success) { documentReplaced(navigationGeneration); mainDocumentLoaded(location) }
        else documentLoadUnsuccessful(navigationGeneration, location)
    }
    onInitializationFailed: function(reason) { failed(reason) }
    onReturnedToQml: exitRequested()
}
