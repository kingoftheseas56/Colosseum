import QtQuick
import QtQuick.Window
import "../../qml/feria"
Window {
    id: root
    visible: true; width: 900; height: 700
    property int phase: 0
    property var saved: null
    function load() {
        FeriaAccount.beginVisit({pk:"webtoon"})
        browser.active = true
        browser.setSource(smokeEngine === "webview2" ? "../../qml/feria/FeriaWebView2.qml" : "../../qml/feria/FeriaQtWebEngine.qml", {
            sourceUrl: smokeUrls[0], profilePath: smokeProfileRoot, observationMode: "read",
            resumeLocator: saved ? saved.locator : null
        })
    }
    Component.onCompleted: load()
    Loader { id: browser; anchors.fill: parent }
    Connections {
        target: browser.item
        function onCompleted(location, success) {
            if (!success || root.phase !== 0) return
            var script = "document.querySelector('#reader').scrollTop=800; true"
            if (smokeEngine === "webview2") browser.item.executeScript("fixture-scroll", script)
            else browser.item.runJavaScript(script)
        }
        function onPlaybackObserved(sample) {
            if (!sample || sample.kind !== "book" || sample.position < 20) return
            if (!FeriaAccount.observe(sample)) return
            if (root.phase === 0) {
                root.saved = FeriaAccount.continueItems[0]
                browserReporter.report("reading", !!root.saved.locator && root.saved.position > 20, "Automatic reading position reaches Continue")
                root.phase = 1; browser.active = false; Qt.callLater(root.load)
            } else {
                browserReporter.report("reading-resume", Math.abs(sample.position-root.saved.position) < 3,
                    "Recreated browser restores the nested reader position")
                browserReporter.finish()
            }
        }
    }
    Timer { running: true; interval: 30000; onTriggered: { browserReporter.report("reading", false, "timeout"); browserReporter.finish() } }
}
