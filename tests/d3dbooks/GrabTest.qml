// Can lanista SEE the book reader? (2026-08-01, Agent 4)
// The lanista plan rests on QQuickItem::grabToImage() — an INSIDE-the-app grab through Qt's
// own scene graph, NOT the outside-the-app paths (GDI/PrintWindow/MCP) that the
// "uncapturable headless" memory ruled out. Two things are unproven and this tests both:
//   1. does grabToImage() work at all on the D3D11 boot?
//   2. does it capture WEBENGINE content, which renders in a separate process and might
//      never land in the scene graph as grabbable pixels?
// If both hold, a scripted OpenGL-vs-D3D11 image diff can confirm Reader 2 completely.
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: win
    width: 900; height: 600; visible: true
    color: "#101014"
    property string arm: (Qt.application.arguments[Qt.application.arguments.length - 1] === "d3d") ? "d3d" : "gl"

    WebEngineView {
        id: view
        anchors.fill: parent
        url: "data:text/html;charset=utf-8," + encodeURIComponent(
            "<html><body style='margin:0;background:#c8102e'>" +
            "<h1 style='color:#fff;font:64px sans-serif;padding:60px'>GRAB ME</h1>" +
            "<div style='width:300px;height:120px;background:#00a3e0;margin-left:60px'></div>" +
            "<script>let n=0;function f(){if(++n===5)document.title='PAINTED';requestAnimationFrame(f);}requestAnimationFrame(f);</script>" +
            "</body></html>")
        onTitleChanged: if (view.title === "PAINTED") grabTimer.start()
    }

    Timer {
        id: grabTimer
        interval: 1200   // let Chromium settle past first paint
        onTriggered: {
            var ok = view.grabToImage(function(result) {
                var path = "tests/d3dbooks/grab-" + win.arm + ".png"
                var saved = result.saveToFile(path)
                console.log("PROBE grabToImage callback fired: saved =", saved,
                            " size =", result.image.width + "x" + result.image.height)
                console.log("PROBE VERDICT =", saved ? "GRAB_OK" : "GRAB_SAVE_FAILED")
                Qt.quit()
            })
            console.log("PROBE grabToImage() returned =", ok)
            if (!ok) { console.log("PROBE VERDICT = GRAB_REFUSED"); Qt.quit() }
        }
    }
    Timer { interval: 12000; running: true; onTriggered: { console.log("PROBE VERDICT = TIMEOUT_NO_CALLBACK"); Qt.quit() } }
}
