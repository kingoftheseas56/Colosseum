import QtQuick
import Colosseum.FeriaHost 1.0
import "FeriaPlayback.js" as Playback
import "FeriaAppMode.js" as AppMode

FeriaHostItem {
    id: browser
    objectName: "feriaWebView2"
    required property url sourceUrl
    required property string profilePath
    property string providerId: ""
    property string accountVisitToken: ""
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
        interval:1500; repeat:true; running:browser.ready && browser.appDocumentReady && !browser.clearingSession && browser.providerId.length > 0
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
        running: browser.ready && (browser.observationEnabled || browser.resumePosition > 0 || browser.resumeLocator !== null)
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
    url: sourceUrl
    userDataFolder: profilePath + "/webview2"
    onNavigationStarted: function(location) { fullScreenActive = false; appDocumentReady = false; ++navigationGeneration; loadActive = true; started(location) }
    onDocumentReady: function(location) { appDocumentReady = true; applyAppMode(); completed(location, true) }
    onNavigationCompleted: function(location, success) { appDocumentReady = success; loadActive = false; if (success) applyAppMode(); completed(location, success) }
    onInitializationFailed: function(reason) { failed(reason) }
    onReturnedToQml: exitRequested()
}
