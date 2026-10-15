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
    property string providerId: ""
    property bool fullScreenActive: false
    Shortcut {
        sequence:"Escape"
        context:Qt.WindowShortcut
        enabled:browser.fullScreenActive
        onActivated:browser.runJavaScript("if(document.fullscreenElement) document.exitFullscreen()")
    }
    property bool appLayout: true
    property bool appKeyboard: true
    property string appAccent: "#f2c94c"
    function applyAppMode() {
        if (providerId.length > 0 && documentReady && !clearingSession) runJavaScript(AppMode.script(providerId,appLayout,appKeyboard,appAccent))
    }
    onAppLayoutChanged: applyAppMode()
    onAppKeyboardChanged: applyAppMode()
    onProviderIdChanged: applyAppMode()
    Timer {
        interval:1500; repeat:true; running:browser.documentReady && !browser.clearingSession && browser.providerId.length > 0
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
    function samplePlayback() {
        var generation = navigationGeneration
        runJavaScript(Playback.observation(observationMode), function(sample) {
            if (!sample || generation !== navigationGeneration) return
            if (resumeLocator && resumeLocator.type === "url") resumeLocator = null
            if (resumeLocator && sample.kind === "book" && Playback.sameDestination(sample.href, sourceUrl)) {
                ++readingResumeAttempts
                runJavaScript(Playback.restoreReading(resumeLocator), function(ok) {
                    if (generation !== navigationGeneration) return
                    if (ok) resumeLocator = null
                    else if (readingResumeAttempts >= 10) { resumeLocator = null; resumeUnavailable() }
                })
                return
            }
            if (sample.kind !== "book" && resumePosition > 0 && Playback.canResume(sample) && Playback.sameDestination(sample.href, sourceUrl)) {
                ++mediaResumeAttempts
                runJavaScript(Playback.seek(resumePosition), function(success) {
                    if (generation !== navigationGeneration) return
                    if (success) resumePosition = 0
                    else if (mediaResumeAttempts >= 10) { resumePosition = 0; resumeUnavailable() }
                })
            } else if (observationEnabled) playbackObserved(sample)
        })
    }
    Timer {
        interval: 2000; repeat: true; running: browser.observationEnabled || browser.resumePosition > 0 || browser.resumeLocator !== null
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
        onTriggered: browser.runJavaScript("window.__feriaStorageCleared || ''", function(result) {
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
    // Website history state. Qt 6.11's view-level history surfaces — the
    // canGoBack/canGoForward properties, goBack()/goForward() and the state of
    // action(WebEngineView.Back/Forward) — only refresh after view-initiated
    // navigations: after an in-page link they keep the last assignment's
    // snapshot, and after crossing origins they stay disabled even though
    // traversal still works. The page's Navigation API is exact within the
    // current origin but origin-limited: cross-origin and error-page entries
    // are invisible to it. The shell state therefore reconciles engine-backed
    // inputs only: the page's history length (covers every origin), the
    // Navigation API's same-origin index, and a count of cross-origin entries
    // known to sit ahead (crossOriginAhead, maintained from engine traversal
    // events; a new navigation clears it with the forward entries). No URLs
    // and no history contents are recorded.
    property bool navCanGoBack: false
    property bool navCanGoForward: false
    property int crossOriginAhead: 0
    property string pendingTraversal: ""
    property string previousOrigin: ""
    property int bookkeptGeneration: -1
    function refreshNavigationState(bookkeep) {
        if (clearingSession) return
        var generation = navigationGeneration
        runJavaScript("(function(){var n=window.navigation,c=n&&n.currentEntry;"
            + "return JSON.stringify({len:history.length,origin:location.origin,"
            + "idx:c?c.index:null,entries:n?n.entries().length:null,"
            + "nav:(performance.getEntriesByType('navigation')[0]||{}).type||''})})()",
            function(result) {
                if (generation !== navigationGeneration || result === undefined) return
                var info
                try { info = JSON.parse(result) } catch (e) { return }
                if (typeof info.len !== "number" || typeof info.origin !== "string") return
                if (bookkeep && bookkeptGeneration !== generation) {
                    bookkeptGeneration = generation
                    if (info.nav === "back_forward") {
                        var crossed = info.origin !== previousOrigin
                        if (pendingTraversal === "forward") {
                            if (crossed) crossOriginAhead = Math.max(0, crossOriginAhead - 1)
                        } else if (crossed) {
                            // Our own back traversal, or a page-initiated one.
                            crossOriginAhead += 1
                        }
                    } else {
                        // A new navigation clears whatever sat ahead of it.
                        crossOriginAhead = 0
                    }
                    pendingTraversal = ""
                    previousOrigin = info.origin
                }
                if (typeof info.idx === "number" && typeof info.entries === "number") {
                    var crossBehind = Math.max(0, info.len - info.entries - crossOriginAhead)
                    var jointIndex = info.idx + crossBehind
                    navCanGoBack = jointIndex > 0
                    navCanGoForward = info.len - 1 > jointIndex
                } else {
                    // Error pages (and documents without a Navigation API)
                    // expose no same-origin index; the newest-entry assumption
                    // matches what the engine's own actions report.
                    navCanGoBack = info.len - 1 - crossOriginAhead > 0
                    navCanGoForward = crossOriginAhead > 0
                }
            })
    }
    Timer {
        id: navigationRefresh
        interval: 400
        onTriggered: browser.refreshNavigationState(false)
    }
    function pageBack() {
        if (!navCanGoBack) return
        pendingTraversal = "back"
        runJavaScript("history.back()")
        navigationRefresh.restart()
    }
    function pageForward() {
        if (!navCanGoForward) return
        pendingTraversal = "forward"
        runJavaScript("history.forward()")
        navigationRefresh.restart()
    }
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
    profile: WebEngineProfile {
        storageName: "feria"
        offTheRecord: false
        persistentStoragePath: browser.profilePath + "/qtwebengine"
        cachePath: browser.profilePath + "/qtwebengine-cache"
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
                runJavaScript(Session.clearStorage)
                cleanupPoll.start()
            }
            // A completed listener may begin cleanup synchronously. Inspect the
            // current load before notifying it, so about:blank is never cleaned.
            completed(String(info.url), true)
            refreshNavigationState(true)
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
            refreshNavigationState(true)
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
            browser.runJavaScript("({ready:document.readyState !== 'loading' && !!document.body && document.body.childElementCount > 0,url:location.href})", function(result) {
                if (generation !== browser.navigationGeneration || !result || result.ready !== true
                        || String(result.url) !== String(browser.url)) return
                documentCheck.stop()
                browser.documentReady = true
                browser.applyAppMode()
                browser.completed(String(browser.url), true)
                browser.refreshNavigationState(true)
            })
        }
    }
    onRenderProcessTerminated: function(status, exitCode) { completed(String(url), false) }
    onFullScreenRequested: function(request) { request.accept(); fullScreenActive = request.toggleOn }
    onNewWindowRequested: function(request) { openPopup(request) }
    function openPopup(request) {
        if (!FeriaBrowserPolicy.allowsNavigation(request.requestedUrl)) return
        var popup = popupComponent.createObject(browser, { "sharedProfile": browser.profile })
        if (!popup) return
        popups = popups.concat([popup])
        request.openIn(popup.view)
        popup.show()
    }
    onSuppressedChanged: {
        if (suppressed) {
            var windows = popups.slice()
            for (var i = 0; i < windows.length; ++i) windows[i].close()
        }
    }
    Component.onDestruction: {
        var windows = popups.slice()
        for (var i = 0; i < windows.length; ++i) windows[i].destroy()
    }
    Component {
        id: popupComponent
        Window {
            id: popup
            title: "Feria — Sign in"
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
                settings.javascriptCanOpenWindows: true
                onNavigationRequested: function(request) {
                    if (!FeriaBrowserPolicy.allowsNavigation(request.url)) request.reject()
                }
                onNewWindowRequested: function(request) { browser.openPopup(request) }
                onWindowCloseRequested: popup.close()
            }
        }
    }
}
