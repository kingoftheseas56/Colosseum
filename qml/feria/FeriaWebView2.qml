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
    property var pendingNavOp: null
    property bool traversalNavigation: false
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
        pendingNavOp = {operationId: admitted.operationId, kind: kind, identity: identity}
        return admitted.operationId
    }
    function settleNavigation(outcome, reason, evidence) {
        var pending = pendingNavOp
        pendingNavOp = null
        if (!pending) return
        operations().settleOperation(pending.identity, pending.operationId, outcome, reason, evidence, {})
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
            // a timeout guess; its terminal callback may follow.
            browser.admitNavigation("stop")
            browser.settleNavigation("cancelled", "user-stop", "user-stop-requested")
            browser.stopLoad(); return true
        }
        function goToOffset(offset, revision) { return false }
    }
    url: sourceUrl
    userDataFolder: profilePath + "/webview2"
    onNavigationStarted: function(location) {
        fullScreenActive = false; appDocumentReady = false; ++navigationGeneration
        loadActive = true; traversalNavigation = false
        // Document generation is canonical state: only a genuine user-facing
        // navigation advances it (the internal close unload never reaches
        // this handler), which also retires any stale captured identity.
        if (!closingSession && sessions && accountSessionId.length)
            sessions.advanceBrowserDocument(accountSessionId)
        started(location)
    }
    onDocumentReady: function(location) { appDocumentReady = true; applyAppMode(); completed(location, true) }
    onNavigationCompleted: function(location, success) {
        appDocumentReady = success; loadActive = false
        if (success) { applyAppMode(); settleNavigation("succeeded", "native-history", "navigation-completed") }
        else settleNavigation("failed", "load-unsuccessful", "navigation-failed")
        completed(location, success)
        if (closingSession) return
        if (success) { documentReplaced(navigationGeneration); mainDocumentLoaded(location) }
        else documentLoadUnsuccessful(navigationGeneration, location)
    }
    onInitializationFailed: function(reason) { failed(reason) }
    onReturnedToQml: exitRequested()
}
