import QtQuick
import QtQuick.Window
import QtWebEngine

WebEngineView {
    id: browser
    objectName: "feriaQtWebEngine"
    required property url sourceUrl
    required property string profilePath
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
            completed(String(info.url), true)
        } else if (info.status === WebEngineView.LoadFailedStatus) {
            ++navigationGeneration
            documentCheck.stop()
            documentReady = false
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
