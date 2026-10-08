import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: root
    width: 900
    height: 680
    visible: true
    title: "THROWAWAY alpha-composition probe | checkerboard, not video"
    color: "#ed008c"
    property int step: 0
    property bool started: false

    // Genuine native Qt Quick scene-graph rectangles, beneath WebEngineView.
    Repeater {
        model: 30 * 30
        Rectangle {
            required property int index
            x: (index % 30) * 40
            y: Math.floor(index / 30) * 40
            width: 40
            height: 40
            color: ((index % 30 + Math.floor(index / 30)) % 2) ? "#f5ba3b" : "#19b8ad"
        }
    }
    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: "transparent"
        url: "qrc:/overlay.html" + (probe.startTransparent ? "#transparent" : "")
        onLoadingChanged: function(info) {
            console.log("WEB LOAD", info.status, info.errorString)
            if (info.status === WebEngineView.LoadSucceededStatus && !root.started) {
                root.started = true
                tick.start()
            }
        }
        onRenderProcessTerminated: function(status, code) {
            console.error("WEB RENDERER TERMINATED", status, code)
        }
        onJavaScriptConsoleMessage: function(level, message, line, source) {
            console.log("HTML", message)
        }
    }
    function capture(name, next) {
        web.runJavaScript("window.measureProbe()", function(dom) {
            probe.capture(name, dom, web.zoomFactor)
            next()
            tick.start()
        })
    }
    function click(id) {
        web.runJavaScript("window.centerOf('"+id+"')", function(p) {
            probe.click(p.x * web.zoomFactor, p.y * web.zoomFactor)
        })
    }
    Timer {
        id: tick
        interval: 800
        repeat: false
        onTriggered: {
            switch (root.step++) {
            case 0:
                root.capture(probe.startTransparent ? "initial-transparent" : "opaque", function() { web.runJavaScript("document.body.classList.remove('opaque')") })
                break
            case 1:
                root.capture("transparent", function() { root.click("menuButton") })
                break
            case 2:
                root.capture("menu", function() { root.click("captionButton") })
                break
            case 3:
                root.capture("caption-off", function() { root.click("captionButton") })
                break
            case 4:
                root.capture("caption-on", function() { root.width = 1000; root.height = 780 })
                break
            case 5:
                root.capture("resized", function() { web.runJavaScript("window.scrollTo(0,60)") })
                break
            case 6:
                root.capture("scrolled", function() { web.zoomFactor = 1.25 })
                break
            case 7:
                root.capture("zoomed", function() {})
                break
            default:
                probe.done()
            }
        }
    }
}
