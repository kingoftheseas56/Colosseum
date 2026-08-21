// scroll_glide_harness — deterministic motion-law and load gate for ScrollGlide.
//
// The production component owns the render-loop clock, but exposes _drainWheel(frameTimeSeconds)
// as a synchronous seam. This harness uses that seam to prove the reader-parity curve without
// sleeping or depending on a particular compositor cadence.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "../qml"

Window {
    visible: true
    width: 480
    height: 360
    color: "#0a0b10"

    Flickable {
        id: f
        anchors.fill: parent
        contentWidth: width
        contentHeight: 2200
        pixelAligned: false
        boundsBehavior: Flickable.StopAtBounds
        Column {
            width: f.width
            Repeater {
                model: 22
                Rectangle {
                    width: f.width
                    height: 100
                    color: index % 2 ? "#10131a" : "#0d1017"
                }
            }
        }
    }
    ScrollGlide { id: glide; flick: f }

    // Keep the original component-load gate as a separate, real Loader instance.
    Flickable {
        id: loadFlick
        width: 1
        height: 1
        contentWidth: 1
        contentHeight: 10
    }
    Loader {
        id: loadGate
        source: "../qml/ScrollGlide.qml"
        onLoaded: item.flick = loadFlick
    }

    // Keep the scrollbar surface in the harness so the existing HouseScrollBar gate remains live.
    Flickable {
        id: barFlick
        width: 120
        height: 220
        contentWidth: width
        contentHeight: 900
        anchors.right: parent.right
        anchors.top: parent.top
        ScrollBar.vertical: HouseScrollBar { flick: barFlick }
    }

    property int fails: 0

    function check(condition, label) {
        if (!condition) {
            console.log("SG_HARNESS_FAIL: " + label)
            fails++
        }
    }

    function drain(frameCount) {
        for (var i = 0; i < frameCount; i++)
            glide._drainWheel(1.0 / 60.0)
    }

    function runChecks() {
        check(loadGate.status === Loader.Ready, "ScrollGlide Loader reached Ready")
        check(Math.abs(glide.speed - 1.4) < 0.0001, "speed preserves reader angle fallback 1.4")
        check(Math.abs(glide.drainFraction - 0.38) < 0.0001, "drainFraction is reader-parity 0.38")
        check(Math.abs(glide.maxBacklogPx - 6000) < 0.0001, "maxBacklogPx is bounded at 6000")
        check(typeof glide.smoothScrollBy === "function", "smoothScrollBy public seam exists")
        check(typeof glide._drainWheel === "function", "synchronous drain seam exists")

        // 1–7. Start away from both boundaries and drain exactly +168px.
        f.contentY = 300
        glide.cancelGlide()
        glide.smoothScrollBy(168)
        var start = f.contentY
        var destination = start + 168
        drain(5)
        var fraction5 = (f.contentY - start) / 168
        check(fraction5 >= 0.90 && fraction5 <= 0.92,
              "five 60Hz drains travel 90–92% (got " + fraction5.toFixed(4) + ")")
        check(Math.abs(f.contentY - Math.round(f.contentY)) > 0.01,
              "motion remains sub-pixel after five drains (got " + f.contentY + ")")

        drain(5)
        var fraction10 = (f.contentY - start) / 168
        check(fraction10 > 0.99 && fraction10 <= 1.0,
              "ten 60Hz drains travel >99% (got " + fraction10.toFixed(4) + ")")

        for (var i = 0; i < 8 && glide._pendingPx !== 0; i++)
            glide._drainWheel(1.0 / 60.0)
        check(glide._pendingPx === 0, "final drain settles pending backlog")
        check(Math.abs(f.contentY - destination) < 0.001,
              "final settling reaches requested floating destination (got " + f.contentY + ")")

        // 8. A hard bottom boundary clamps and drops hidden momentum.
        var maxY = f.contentHeight - f.height
        f.contentY = maxY - 10
        glide.cancelGlide()
        glide.smoothScrollBy(168)
        glide._drainWheel(1.0 / 60.0)
        check(Math.abs(f.contentY - maxY) < 0.001,
              "boundary clamp stops exactly at maxY (got " + f.contentY + ")")
        check(glide._pendingPx === 0, "boundary clamp clears pending backlog")

        // 9. A manual reposition wins over stale wheel momentum.
        f.contentY = 400
        glide.cancelGlide()
        glide.smoothScrollBy(168)
        f.contentY = 777
        check(glide._pendingPx === 0, "external reposition cancels stale backlog")
        glide._drainWheel(1.0 / 60.0)
        check(Math.abs(f.contentY - 777) < 0.001,
              "external reposition remains authoritative after drain")

        // A short input followed by a hitch must still present its sub-pixel residual rather
        // than silently dropping the last fraction of a pixel at the settle threshold.
        f.contentY = 900
        glide.cancelGlide()
        glide.smoothScrollBy(2)
        glide._drainWheel(1.0 / 60.0)
        glide._drainWheel(0.05)
        check(glide._pendingPx > 0 && glide._pendingPx < glide.settleEpsilonPx,
              "hitch leaves only a sub-pixel residual before final settle")
        glide._drainWheel(1.0 / 60.0)
        check(Math.abs(f.contentY - 902) < 0.001,
              "sub-pixel residual settles to the exact requested position")

        // Absolute page/home/end commands are additive wrappers used by the dirty GridView
        // consumers. They must replace, not compound with, an in-flight wheel backlog.
        f.contentY = 400
        glide.cancelGlide()
        glide.smoothScrollBy(168)
        glide._animateTo(0)
        for (var j = 0; j < 30 && glide._pendingPx !== 0; j++)
            glide._drainWheel(1.0 / 60.0)
        check(Math.abs(f.contentY) < 0.001,
              "absolute command replaces stale wheel target (got " + f.contentY + ")")

        if (fails === 0) {
            console.log("SCROLLGLIDE LOAD PASS")
            console.log("SG_HARNESS_PASS")
            Qt.exit(0)
        } else {
            console.log("SG_HARNESS_FAILS: " + fails)
            Qt.exit(1)
        }
    }

    Timer {
        id: loadPoll
        interval: 1
        repeat: true
        running: true
        onTriggered: {
            if (loadGate.status === Loader.Ready) {
                stop()
                runChecks()
            }
        }
    }

    Timer {
        interval: 3000
        running: true
        onTriggered: {
            console.log("SG_HARNESS_FAIL: timeout waiting for Loader")
            Qt.exit(1)
        }
    }
}
