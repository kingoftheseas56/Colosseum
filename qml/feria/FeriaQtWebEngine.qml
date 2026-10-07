// Lifecycle handling adapted from Qt Lifecycle Browser; see NanoBrowser-LICENSE.txt.
// Copyright (C) 2026 The Qt Company Ltd.
import QtQuick
import QtQuick.Window
import QtWebEngine
import "FeriaPlayback.js" as Playback
import "FeriaSession.js" as Session
import "FeriaAppMode.js" as AppMode

WebEngineView {
    id: browser
    objectName: "feriaQtWebEngine"
    required property url sourceUrl
    required property string profilePath
    property var sharedProfile: null
    readonly property alias pageTools: pageToolsController
    FeriaPageTools { id: pageToolsController; browser: browser }
    readonly property alias browserRequests: requestsController
    signal message(string text)
    signal tabRequested(var request)
    FeriaBrowserRequests {
        id: requestsController
        browser: browser
        onMessage: (text) => browser.message(text)
    }
    // Adapted from Qt's BSD-licensed Lifecycle Browser. Only freeze when the
    // engine says it is safe; audible pages and pending callbacks stay active.
    property bool freezeBackground: true
    property int freezeDelay: 30000
    function wakePage() {
        freezeTimer.stop()
        if (lifecycleState !== WebEngineView.LifecycleState.Active)
            lifecycleState = WebEngineView.LifecycleState.Active
    }
    onVisibleChanged: if (visible) wakePage()
    onFreezeBackgroundChanged: if (!freezeBackground) wakePage()
    Timer {
        id: freezeTimer
        interval: browser.freezeDelay
        running: browser.freezeBackground && !browser.visible && !browser.closingSession
            && !browser.recentlyAudible
            && browser.recommendedState !== WebEngineView.LifecycleState.Active
            && browser.lifecycleState === WebEngineView.LifecycleState.Active
        onTriggered: {
            if (browser.pendingJavaScript === 0) browser.lifecycleState = WebEngineView.LifecycleState.Frozen
            else restart()
        }
    }
    // Drain callbacks before the owner destroys this view. In Qt 6.11.1 a
    // pending JavaScript callback can reach a torn-down QML context on close.
    property bool closingSession: false
    property bool adoptingWindowRequest: false
    property int pendingJavaScript: 0
    signal safeToDestroy()
    function evaluateJavaScript(script, callback) {
        if (closingSession || adoptingWindowRequest) return false
        if (lifecycleState !== WebEngineView.LifecycleState.Active) return false
        // Legacy single-view and cleanup owners retain their existing API.
        if (!sharedProfile) {
            if (typeof callback === "function") runJavaScript(script, callback)
            else runJavaScript(script)
            return true
        }
        ++pendingJavaScript
        runJavaScript(script, function(result) {
            try {
                if (!browser.closingSession && typeof callback === "function") callback(result)
            } finally {
                --browser.pendingJavaScript
                if (browser.closingSession && browser.pendingJavaScript === 0) {
                    browser.stop()
                    browser.safeToDestroy()
                }
            }
        })
        return true
    }
    function prepareForClose() {
        if (closingSession) return
        wakePage()
        // Pause without depending on a provider-specific connector, then drain
        // callbacks before stopping the engine or destroying its QML context.
        evaluateJavaScript("document.querySelectorAll('video,audio').forEach(function(m){m.pause()})")
        closingSession = true
        // Release focus while the retained owner's scope is still alive,
        // before deferred parent/Loader destruction reaches the native view.
        focus = false
        documentCheck.stop(); cleanupPoll.stop()
        if (pendingJavaScript === 0) { stop(); safeToDestroy() }
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
        if (providerId.length > 0 && documentReady && !clearingSession) evaluateJavaScript(AppMode.script(providerId,appLayout,appKeyboard,appAccent))
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
        running: !browser.closingSession && browser.lifecycleState === WebEngineView.LifecycleState.Active
            && (browser.observationEnabled || browser.resumePosition > 0 || browser.resumeLocator !== null)
        onTriggered: browser.samplePlayback()
    }
    signal localSessionCleared(bool success)
    property bool clearingSession: false
    property var clearOrigins: []
    property var clearDomains: []
    property bool cleanupNavigationPending: false
    function clearLocalSession(domains, origins) {
        clearingSession = true; clearDomains = domains; clearOrigins = origins.slice(); clearNextOrigin()
    }
    function clearNextOrigin() {
        if (clearOrigins.length === 0) {
            clearingSession = false; localSessionCleared(true)
            return
        }
        var origin = clearOrigins[0]; clearOrigins = clearOrigins.slice(1)
        // This blank page has the site's origin but executes no provider scripts
        // and makes no network request. It can clear origin storage offline.
        cleanupNavigationPending = true
        loadHtml("<!doctype html><title>Feria session cleanup</title>", origin + "/")
    }
    Timer {
        id: cleanupPoll; interval: 100; repeat: true
        onTriggered: browser.evaluateJavaScript("window.__feriaStorageCleared || ''", function(result) {
            if (result === "done") { cleanupPoll.stop(); browser.clearNextOrigin() }
            else if (result === "failed") {
                cleanupPoll.stop(); browser.clearingSession = false; browser.localSessionCleared(false)
            }
        })
    }
    property bool suppressed: false
    readonly property bool ready: true
    property var popups: []
    property bool documentReady: false
    property int navigationGeneration: 0
    // Engine load state for the shell; WebEngineView reports real loadProgress.
    readonly property bool loadActive: loading
    property bool stopRequested: false
    function stopLoad() { if (loading) { stopRequested = true; stop() } }
    readonly property alias navigation: navigationController
    FeriaNavigation { id: navigationController; browser: browser }
    readonly property bool navCanGoBack: navigationController.canGoBack
    readonly property bool navCanGoForward: navigationController.canGoForward
    function pageBack() { return navigationController.back() }
    function pageForward() { return navigationController.forward() }
    signal started(string location)
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
        if (closingSession || adoptingWindowRequest) return
        if (info.status === WebEngineView.LoadStartedStatus) {
            fullScreenActive = false
            stopRequested = false
            ++navigationGeneration
            documentReady = false
            documentCheck.restart()
            started(String(info.url))
        }
        else if (info.status === WebEngineView.LoadSucceededStatus) {
            stopRequested = false
            documentCheck.stop()
            documentReady = true
            applyAppMode()
            if (clearingSession) {
                evaluateJavaScript(Session.clearStorage)
                cleanupPoll.start()
            }
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
            stopRequested = false
            ++navigationGeneration
            documentCheck.stop()
            documentReady = false
            if (clearingSession) { cleanupPoll.stop(); clearingSession = false; localSessionCleared(false) }
            completed(String(info.url), true)
        } else if (info.status === WebEngineView.LoadFailedStatus) {
            ++navigationGeneration
            documentCheck.stop()
            documentReady = false
            if (clearingSession) { cleanupPoll.stop(); clearingSession = false; localSessionCleared(false) }
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
                if (generation !== browser.navigationGeneration || !result || result.ready !== true
                        || String(result.url) !== String(browser.url)) return
                documentCheck.stop()
                browser.documentReady = true
                browser.applyAppMode()
                browser.completed(String(browser.url), true)
            })
        }
    }
    onRenderProcessTerminated: function(status, exitCode) { completed(String(url), false) }
    onFullScreenRequested: function(request) { request.accept(); fullScreenActive = request.toggleOn }
    onNewWindowRequested: function(request) {
        if (!request.userInitiated) { message("Popup blocked"); return }
        if (sharedProfile && (request.destination === WebEngineNewWindowRequest.InNewTab
            || request.destination === WebEngineNewWindowRequest.InNewBackgroundTab))
            tabRequested(request)
        else openPopup(request)
    }
    function openPopup(request) {
        if (!FeriaBrowserPolicy.allowsNavigation(request.requestedUrl)) return
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
        var windows = popups.slice()
        for (var i = 0; i < windows.length; ++i) windows[i].destroy()
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

