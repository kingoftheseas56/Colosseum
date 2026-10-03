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
    property double mediaLoadedAt: 0
    property bool advancing: false
    property var results: []
    function fixtureStatus(status) {
        if (String(smokeUrls[index]).endsWith('/fixture/session-set') && status.stage && status.stage !== 'done' && !status.detail) {
            // Storage and service-worker setup is asynchronous. Keep the existing
            // deadline, and wait for the fixture's readiness flag rather than a fixed delay.
            settleTimer.restart()
        } else finishOne(false, JSON.stringify(status))
    }
    function next() {
        settling = false
        mediaLoadedAt = 0
        ++index
        if (index >= smokeUrls.length) { browserReporter.finish(); return }
        deadline.restart()
        browser.setSource(smokeEngine === "webview2"
            ? "../../qml/feria/FeriaWebView2.qml" : "../../qml/feria/FeriaQtWebEngine.qml", {
            sourceUrl: smokeUrls[index], profilePath: smokeProfileRoot,
            resumePosition: String(smokeUrls[index]).indexOf("/fixture/media") >= 0 ? 10 : 0
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
            if (String(smokeUrls[window.index]).indexOf("/fixture/media") < 0) return
            if (sample && sample.duration >= 30 && sample.position >= 9 && !sample.paused)
                window.finishOne(window.mediaLoadedAt > 0 && Date.now()-window.mediaLoadedAt < 9000, "Playback observation and seek verified before natural playback reaches the saved position")
        }
        function onFailed(reason) { window.finishOne(false, reason) }
        function onCompleted(location, success) {
            if (String(smokeUrls[window.index]).indexOf("/fixture/media") >= 0 && success) { if (!window.mediaLoadedAt) window.mediaLoadedAt = Date.now(); return }
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
                    if (passed === true) window.finishOne(true, result.location)
                    else browser.item.runJavaScript("({detail:document.body.dataset.detail || '',stage:document.body.dataset.stage || ''})", function(status) {
                        window.fixtureStatus(status)
                    })
                })
            } else window.finishOne(result.success, result.location)
        }
    }
    Connections {
        target: smokeEngine === "webview2" ? browser.item : null
        function onScriptResult(label, jsonResult) {
            if (label === "fixture-check") {
                if (jsonResult === "true") window.finishOne(true,"DOM, redirect, cookie or popup check")
                else browser.item.executeScript("fixture-stage","({detail:document.body.dataset.detail || '',stage:document.body.dataset.stage || ''})")
            }
            else if (label === "fixture-stage") window.fixtureStatus(JSON.parse(jsonResult))
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
