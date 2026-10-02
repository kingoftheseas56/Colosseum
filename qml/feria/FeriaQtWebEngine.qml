import QtQuick
import QtQuick.Window
import QtWebEngine
import "FeriaPlayback.js" as Playback
import "FeriaSession.js" as Session

WebEngineView {
    id: browser
    objectName: "feriaQtWebEngine"
    required property url sourceUrl
    required property string profilePath
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
            if (sample.kind !== "book" && resumePosition > 0 && sample.duration > 0 && !sample.ad && Playback.sameDestination(sample.href, sourceUrl)) {
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
    signal started(string location)
    signal completed(string location, bool success)
    signal failed(string reason)
    signal exitRequested()
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
            ++navigationGeneration
            documentReady = false
            documentCheck.restart()
            started(String(info.url))
        }
        else if (info.status === WebEngineView.LoadSucceededStatus) {
            documentCheck.stop()
            documentReady = true
            if (clearingSession) {
                runJavaScript(Session.clearStorage)
                cleanupPoll.start()
            }
            // A completed listener may begin cleanup synchronously. Inspect the
            // current load before notifying it, so about:blank is never cleaned.
            completed(String(info.url), true)
        } else if (info.status === WebEngineView.LoadFailedStatus) {
            ++navigationGeneration
            documentCheck.stop()
            documentReady = false
            if (clearingSession) { cleanupPoll.stop(); clearingSession = false; localSessionCleared(false) }
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
                browser.completed(String(browser.url), true)
            })
        }
    }
    onRenderProcessTerminated: function(status, exitCode) { completed(String(url), false) }
    onFullScreenRequested: function(request) { request.accept() }
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
