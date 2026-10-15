// Books-under-Direct3D probe (2026-08-01, Agent 4).
// The app asserts "Chromium needs OpenGL, so Player 2's D3D11 can never coexist with the
// book reader" — asserted in main.cpp, never tested. Run this SAME file twice through
// colosseum.exe (which calls QtWebEngineQuick::initialize()): once plain (OpenGL) and once
// with COLOSSEUM_PLAYER2=1 (Direct3D11). Anything that only fails in the D3D11 arm is the
// real cost of the flip. Reader 2 is a WebEngineView over a web channel, so this IS the
// reader's engine, minus its content.
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: win
    width: 900; height: 600; visible: true
    title: "books-d3d-probe"
    color: "#101014"

    property int paintedCount: 0
    property string verdict: "PENDING"

    WebEngineView {
        id: view
        anchors.fill: parent
        // Self-reporting page: paints a solid known colour and flips the title when its
        // own rAF has actually run — i.e. Chromium composited a frame, not merely parsed HTML.
        url: "data:text/html;charset=utf-8," + encodeURIComponent(
            "<html><body style='margin:0;background:#c8102e'>" +
            "<h1 style='color:#fff;font:48px sans-serif;padding:40px'>BOOKS RENDER TEST</h1>" +
            "<script>let n=0;function f(){if(++n===5){document.title='PAINTED';}requestAnimationFrame(f);}requestAnimationFrame(f);</script>" +
            "</body></html>")

        onLoadingChanged: function(req) {
            console.log("PROBE loadStatus =", req.status, " errorString =", req.errorString)
            if (req.status === WebEngineView.LoadFailedStatus)
                win.verdict = "LOAD_FAILED"
        }
        onTitleChanged: {
            console.log("PROBE title ->", view.title)
            if (view.title === "PAINTED" && win.verdict === "PENDING")
                win.verdict = "CHROMIUM_COMPOSITED"
        }
        onRenderProcessTerminated: function(status, code) {
            console.log("PROBE RENDER PROCESS TERMINATED status =", status, " code =", code)
            win.verdict = "RENDER_PROCESS_DIED"
        }
    }

    Timer {
        interval: 9000; running: true
        onTriggered: {
            console.log("PROBE VERDICT =", win.verdict)
            Qt.quit()
        }
    }
}
