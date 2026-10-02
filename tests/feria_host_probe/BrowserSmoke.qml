import QtQuick
import QtQuick.Window
import "../../qml/feria"

Window {
    id: window
    width: 1100
    height: 720
    visible: true
    property int index: -1
    property bool settling: false
    property bool advancing: false
    property var results: []
    function next() {
        settling = false
        ++index
        if (index >= smokeUrls.length) { browserReporter.finish(); return }
        deadline.restart()
        browser.setSource(smokeEngine === "webview2"
            ? "../../qml/feria/FeriaWebView2.qml" : "../../qml/feria/FeriaQtWebEngine.qml", {
            sourceUrl: smokeUrls[index], profilePath: smokeProfileRoot,
            resumePosition: String(smokeUrls[index]).endsWith("/fixture/media") ? 10 : 0
        })
        browser.active = true
        advancing = false
    }
    function finishOne(success, detail) {
        if (advancing) return
        advancing = true
        deadline.stop()
        settleTimer.stop()
        inspectionDeadline.stop()
        browserReporter.report(smokeUrls[index], success, detail)
        browser.active = false
        Qt.callLater(next)
    }
    Component.onCompleted: next()
    Loader { id: browser; anchors.fill: parent }
    Connections {
        target: browser.item
        function onPlaybackObserved(sample) {
            if (!String(smokeUrls[window.index]).endsWith("/fixture/media")) return
            if (sample && sample.duration >= 30 && sample.position >= 9 && !sample.paused)
                window.finishOne(true, "Playback observation and resume position verified")
        }
        function onFailed(reason) { window.finishOne(false, reason) }
        function onCompleted(location, success) {
            if (String(smokeUrls[window.index]).endsWith("/fixture/media") && success) return
            if (window.settling || window.advancing) return
            window.settling = true
            window.results.push({ location: location, success: success })
            settleTimer.restart()
        }
    }
    Timer {
        id: settleTimer
        interval: 1500
        onTriggered: {
            var result = window.results[window.results.length - 1]
            if (String(smokeUrls[window.index]).indexOf("/fixture/") >= 0 && result.success) {
                var script = "Boolean(document.body.dataset.passed === 'true')"
                if (smokeEngine === "webview2") browser.item.executeScript("fixture-check", script)
                else browser.item.runJavaScript(script, function(passed) {
                    window.finishOne(passed === true, result.location)
                })
            } else window.finishOne(result.success, result.location)
        }
    }
    Connections {
        target: smokeEngine === "webview2" ? browser.item : null
        function onScriptResult(label, jsonResult) {
            if (label === "fixture-check") window.finishOne(jsonResult === "true", "DOM, redirect, cookie or popup check")
            else if (label === "timeout-inspect") window.finishOne(false, "Timeout: " + jsonResult)
        }
    }
    Timer {
        id: deadline
        interval: 30000
        onTriggered: {
            var script = "JSON.stringify({url:location.href,title:document.title,state:document.readyState,text:document.body?document.body.innerText.slice(0,200):''})"
            if (!browser.item) { window.finishOne(false, "Browser did not initialize"); return }
            if (smokeEngine === "webview2") browser.item.executeScript("timeout-inspect", script)
            else browser.item.runJavaScript(script, function(result) { window.finishOne(false, "Timeout: " + result) })
            inspectionDeadline.restart()
        }
    }
    Timer { id: inspectionDeadline; interval: 3000; onTriggered: window.finishOne(false, "Timed out without a document") }
}
