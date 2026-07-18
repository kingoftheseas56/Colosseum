// wheel_latency_harness — TEMPORARY diagnostic (Agent 1, 2026-07-17). Replicates the manga
// reader's two wheel paths byte-for-byte and timestamps every hop, so injected wheel events
// reveal where the wheel→screen dead time lives. Drive with wheel_latency_drive.ps1.
// Logs: WHEEL <evTs> <nowMs> | FRAME <nowMs> <frameTimeMs> <step> | PAN <nowMs>
import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 800; height: 600
    x: 0; y: 0
    visible: true
    title: "WheelLatencyHarness"
    color: "#0a0b10"
    flags: Qt.Window | Qt.WindowStaysOnTopHint   // injected wheel routes to window under cursor
    // self-quit → clean exit → stderr buffers flush (taskkill /F loses the log)
    Timer { interval: 20000; running: true; onTriggered: Qt.quit() }

    // ---- replica of the reader's long-strip drain (MangaReader.qml smoothScrollBy) ----
    property real _pendingPx: 0
    property real _smoothY: 0
    readonly property real _drainFraction: 0.38
    readonly property real _maxBacklogPx: 6000
    property real contentY: 0

    // ---- replica of the double-page direct pan ----
    property real panY: 0

    property bool _drainFresh: false
    FrameAnimation {
        id: scrollDrain
        running: false
        onTriggered: {
            if (Math.abs(win.contentY - win._smoothY) > 1.5) win._smoothY = win.contentY
            var frames = Math.min(3, Math.max(0.25, scrollDrain.frameTime * 60))
            if (win._drainFresh) { frames = Math.max(1, frames); win._drainFresh = false }
            var step = win._pendingPx * (1 - Math.pow(1 - win._drainFraction, frames))
            if (Math.abs(win._pendingPx) <= 1) step = win._pendingPx
            var y = win._smoothY + step
            var hmax = 4000
            if (y < 0) { y = 0; win._pendingPx = 0 }
            else if (y > hmax) { y = hmax; win._pendingPx = 0 }
            else win._pendingPx -= step
            win._smoothY = y
            win.contentY = y
            console.log("FRAME " + Date.now() + " " + (scrollDrain.frameTime * 1000).toFixed(1) + " " + step.toFixed(1))
            if (win._pendingPx === 0) scrollDrain.stop()
        }
    }
    function smoothScrollBy(dy) {
        if (!scrollDrain.running) { win._smoothY = win.contentY; win._drainFresh = true }
        win._pendingPx = Math.max(-win._maxBacklogPx, Math.min(win._maxBacklogPx, win._pendingPx + dy))
        scrollDrain.start()
    }

    Rectangle {
        anchors.fill: parent
        color: "#0a0b10"
        // the reader's exact handler shape
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: (e) => {
                console.log("WHEEL " + e.timestamp + " " + Date.now())
                var dy = e.pixelDelta.y !== 0 ? -e.pixelDelta.y : -e.angleDelta.y * 1.4
                win.smoothScrollBy(dy)               // strip path
                win.panY += e.angleDelta.y * 0.8     // direct pan path
            }
        }
        // visible movement for both paths (verifies frames actually present)
        Rectangle { x: 100; y: 100 - (win.contentY % 400); width: 200; height: 200; color: "#7a2f49" }
        Rectangle { x: 450; y: 100 + (win.panY % 400); width: 200; height: 200; color: "#5a3f2f" }
        Text { text: "contentY " + win.contentY.toFixed(0) + "  panY " + win.panY.toFixed(0)
               color: "white"; anchors.bottom: parent.bottom; anchors.margins: 12; anchors.left: parent.left }
    }
    onPanYChanged: console.log("PAN " + Date.now())
}
