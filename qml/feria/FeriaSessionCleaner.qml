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
    visible: false
    width: 1; height: 1
    function finish(success) {
        if (done) return
        done = true; deadline.stop(); browser.active = false; finished(success)
    }
    function loadEngine() {
        started = false
        if (engineIndex >= engines.length) { finish(true); return }
        if (engines[engineIndex] === "qtwebengine") {
            cookieRequest = FeriaBrowserPolicy.clearQtCookies(profilePath, domains)
            return
        }
        loadBrowser()
    }
    function loadBrowser() {
        browser.active = true
        browser.setSource(engines[engineIndex] === "webview2" ? "FeriaWebView2.qml" : "FeriaQtWebEngine.qml", {
            sourceUrl: "about:blank", profilePath: profilePath, observationEnabled: false, suppressed: true
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
        onStatusChanged: if (status === Loader.Error) root.finish(false)
    }
    Connections {
        target: browser.item
        function onCompleted(location, success) {
            if (!success || root.started || root.done) return
            root.started = true
            browser.item.clearLocalSession(root.domains, root.origins)
        }
        function onFailed(reason) { root.finish(false) }
        function onLocalSessionCleared(success) {
            if (!success) { root.finish(false); return }
            browser.active = false; ++root.engineIndex; Qt.callLater(root.loadEngine)
        }
    }
    Timer { id: deadline; interval: 60000; onTriggered: root.finish(false) }
}
