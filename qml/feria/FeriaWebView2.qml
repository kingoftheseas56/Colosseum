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
    property int mediaResumeAttempts: 0
    property int navigationGeneration: 0
    function samplePlayback() { executeScript("feria-account-sample-" + navigationGeneration, Playback.observation(observationMode)) }
    Timer {
        interval: 2000; repeat: true; running: browser.ready && (browser.observationEnabled || browser.resumePosition > 0 || browser.resumeLocator !== null)
        onTriggered: browser.samplePlayback()
    }
    onScriptResult: function(label, json) {
        if (label === "feria-account-sample-" + navigationGeneration) {
            try {
                var sample = JSON.parse(json)
                if (!sample) return
                if (resumeLocator && resumeLocator.type === "url") resumeLocator = null
                if (resumeLocator && sample.kind === "book" && Playback.sameDestination(sample.href, sourceUrl)) {
                    ++readingResumeAttempts
                    executeScript("feria-reading-resume-" + navigationGeneration, Playback.restoreReading(resumeLocator))
                    return
                }
                if (sample.kind !== "book" && resumePosition > 0 && sample.duration > 0 && !sample.ad && Playback.sameDestination(sample.href, sourceUrl)) {
                    ++mediaResumeAttempts
                    executeScript("feria-account-resume-" + navigationGeneration, Playback.seek(resumePosition))
                } else if (browser.observationEnabled) playbackObserved(sample)
            } catch (e) {}
        } else if (label === "feria-reading-resume-" + navigationGeneration) {
            if (json === "true") resumeLocator = null
            else if (readingResumeAttempts >= 10) { resumeLocator = null; resumeUnavailable() }
        } else if (label === "feria-account-resume-" + navigationGeneration) {
            if (json === "true") resumePosition = 0
            else if (mediaResumeAttempts >= 10) { resumePosition = 0; resumeUnavailable() }
        }
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
    onAppMenuRequested: optionsRequested()
    url: sourceUrl
    userDataFolder: profilePath + "/webview2"
    onNavigationStarted: function(location) { fullScreenActive = false; appDocumentReady = false; ++navigationGeneration; started(location) }
    onDocumentReady: function(location) { appDocumentReady = true; applyAppMode(); completed(location, true) }
    onNavigationCompleted: function(location, success) { appDocumentReady = success; if (success) applyAppMode(); completed(location, success) }
    onInitializationFailed: function(reason) { failed(reason) }
    onReturnedToQml: exitRequested()
}
