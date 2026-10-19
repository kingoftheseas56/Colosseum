import QtQuick

// Opens blank browser contexts only. No provider page or credentials enter QML.
Item {
    id: root
    required property string profilePath
    required property var domains
    required property var origins
    property var engines: FeriaBrowserPolicy.webView2Available ? ["webview2", "qtwebengine"] : ["qtwebengine"]
    signal finished(bool success)
    property int engineIndex: 0
    property int cookieRequest: -1
    property bool started: false
    property bool done: false
    property bool releasing: false
    property var releaseView: null
    property var releaseAfter: null
    property int releaseToken: 0
    visible: false
    width: 1; height: 1
    // A QtWebEngine view can outlive the cleanup result: script callbacks
    // must drain before Loader destroys the QML context that owns them.
    // WebView2 exposes no drain protocol and keeps its existing teardown.
    function releaseBrowser(next) {
        releaseAfter = next
        if (releasing) return
        releasing = true
        releaseView = browser.item
        ++releaseToken
        if (releaseView && typeof releaseView.prepareForClose === "function") {
            drainDeadline.restart()
            releaseView.prepareForClose(false)
        } else {
            completeRelease(releaseToken)
        }
    }
    function completeRelease(token) {
        if (!releasing || token !== releaseToken) return
        drainDeadline.stop()
        releasing = false
        releaseView = null
        browser.active = false
        var next = releaseAfter
        releaseAfter = null
        if (next) next()
    }
    function finish(success) {
        if (done) return
        done = true; deadline.stop()
        releaseBrowser(function() { root.finished(success) })
    }
    function loadEngine() {
        if (done) return
        started = false
        if (engineIndex >= engines.length) { finish(true); return }
        if (engines[engineIndex] === "qtwebengine") {
            cookieRequest = FeriaBrowserPolicy.clearQtCookies(profilePath, domains)
            return
        }
        loadBrowser()
    }
    function loadBrowser() {
        // Do not replace the engine until its old view has drained.
        releaseBrowser(function() {
            if (root.done) return
            browser.setSource(engines[engineIndex] === "webview2" ? "FeriaWebView2.qml" : "FeriaQtWebEngine.qml", {
                sourceUrl: "about:blank", profilePath: profilePath, observationEnabled: false, suppressed: true
            })
            if (!root.done) browser.active = true
        })
    }
    Connections {
        target: FeriaBrowserPolicy
        function onCookiesCleared(request, success) {
            if (request !== root.cookieRequest || root.done) return
            if (success) root.loadBrowser()
            else root.finish(false)
        }
    }
    Component.onCompleted: { deadline.start(); loadEngine() }
    Loader {
        id: browser; anchors.fill: parent
        onStatusChanged: if (status === Loader.Error && !root.releasing) root.finish(false)
    }
    Connections {
        target: browser.item
        ignoreUnknownSignals: true
        function onSafeToDestroy() {
            if (!root.releasing || root.releaseView !== browser.item) return
            var token = root.releaseToken
            // The signal can fire inside the engine's script callback. Never
            // destroy the view until that callback has returned to the loop.
            Qt.callLater(function() { root.completeRelease(token) })
        }
        function onCompleted(location, success) {
            if (!success || root.started || root.done) return
            root.started = true
            browser.item.clearLocalSession(root.domains, root.origins)
        }
        function onFailed(reason) { if (!root.releasing) root.finish(false) }
        function onLocalSessionCleared(success) {
            if (root.done || root.releasing) return
            if (!success) { root.finish(false); return }
            root.releaseBrowser(function() {
                ++root.engineIndex
                Qt.callLater(root.loadEngine)
            })
        }
    }
    Timer {
        id: drainDeadline
        interval: 5000
        onTriggered: {
            if (!root.releasing || !root.releaseView) return
            var view = root.releaseView
            console.warn("Feria sign-out: cleanup view did not drain; discarding web contents before release")
            if (typeof view.discardForExit === "function" && !view.discardForExit())
                console.warn("Feria sign-out: cleanup view could not enter discarded state")
            var token = root.releaseToken
            Qt.callLater(function() { root.completeRelease(token) })
        }
    }
    Timer { id: deadline; interval: 60000; onTriggered: root.finish(false) }
}
