// Public landing-page compatibility only; run with a disposable smokeProfileRoot.
// A rendered page does not establish sign-in, entitlement, DRM or playback support.
import QtQuick
import QtQuick.Window
import "../../qml/feria/FeriaAppMode.js" as Apps

Window {
    id: window
    width: 1280; height: 800; visible: true
    property int index: -1
    property bool advancing: false
    property bool inspecting: false
    property bool expired: false
    property bool loadSucceeded: false
    property string navigationError: ""
    function providerFor(url) {
        var host = String(url).replace(/^https?:\/\//,'').split('/')[0].split(':')[0]
        var selected = "", length = 0
        Object.keys(Apps.profiles).forEach(function(id) {
            Apps.profiles[id].domains.forEach(function(domain) {
                if ((host === domain || host.endsWith('.'+domain)) && domain.length > length) { selected = id; length = domain.length }
            })
        })
        return selected
    }
    function next() {
        if (++index >= smokeUrls.length) { browserReporter.finish(); return }
        advancing = false; inspecting = false; expired = false; loadSucceeded = false; navigationError = ""
        browser.setSource(smokeEngine === "webview2"
            ? "../../qml/feria/FeriaWebView2.qml" : "../../qml/feria/FeriaQtWebEngine.qml", {
                sourceUrl:smokeUrls[index], profilePath:smokeProfileRoot, observationEnabled:false,
                providerId:window.providerFor(smokeUrls[index])
            })
        browser.active = true
        deadline.restart()
    }
    function finish(result) {
        if (advancing) return
        advancing = true; deadline.stop(); settle.stop(); scriptDeadline.stop()
        browserReporter.report(smokeUrls[index], result.state === "page" || result.state === "sign-in", JSON.stringify(result))
        browser.active = false
        Qt.callLater(next)
    }
    function inspect() {
        if (advancing || inspecting) return
        if (!browser.item) { finish({state:"browser-unavailable"}); return }
        inspecting = true
        scriptDeadline.restart()
        var script = `(function() {
            var text = document.body ? document.body.innerText.replace(/\\s+/g,' ').trim() : '';
            var title = document.title || '';
            var lead = (title + ' ' + text.slice(0,700)).toLowerCase();
            var state = /\\/geo-availability\\//.test(location.pathname) ? 'region-unavailable'
                : /access denied|403 forbidden|verify you are human|checking your browser|just a moment|unusual traffic|pardon our interruption/.test(lead) ? 'challenge'
                : /not available in your (country|region)|not available in (this|your) (country|region)|not supported in your (country|region)|isn.t available in your (country|region)/.test(lead) ? 'region-unavailable'
                : text.length < 50 ? 'empty'
                : /sign.?in|login/.test(location.pathname + ' ' + title) ? 'sign-in' : 'page';
            return {state:state,url:location.origin+location.pathname,title:title,characters:text.length,
                summary:text.slice(0,350),ready:document.readyState,
                app:document.documentElement.getAttribute('data-feria-app') || '',
                adapted:!!document.getElementById('feria-app-layout'),
                focusable:document.querySelectorAll('a[href],button,[role=button]').length};
        })()`
        if (smokeEngine === "webview2") browser.item.executeScript("provider-inspect",script)
        else browser.item.runJavaScript(script, function(result) { window.completeInspection(result) })
    }
    function completeInspection(result) {
        if (!result) result = {state:"no-document"}
        if (!expired && (result.state === "empty" || result.state === "no-document")) {
            inspecting = false; scriptDeadline.stop(); settle.restart(); return
        }
        result.navigationSucceeded = loadSucceeded
        if (navigationError) result.navigationError = navigationError
        finish(result)
    }
    Component.onCompleted: next()
    Loader { id: browser; anchors.fill: parent }
    Connections {
        target: browser.item
        function onStarted(location) { settle.stop() }
        function onCompleted(location, success) { window.loadSucceeded = success; settle.restart() }
        function onFailed(reason) { window.navigationError = reason; window.inspect() }
    }
    Connections {
        target: smokeEngine === "webview2" ? browser.item : null
        function onScriptResult(label, json) {
            if (label !== "provider-inspect") return
            try { window.completeInspection(JSON.parse(json)) }
            catch (e) { window.finish({state:"inspection-error"}) }
        }
    }
    Timer { id: settle; interval: 4000; onTriggered: window.inspect() }
    Timer { id: deadline; interval: 60000; onTriggered: { window.expired = true; window.inspect() } }
    Timer {
        id: scriptDeadline; interval: 5000
        onTriggered: {
            if (window.expired) window.finish({state:"inspection-timeout"})
            else { window.inspecting = false; settle.restart() }
        }
    }
}
