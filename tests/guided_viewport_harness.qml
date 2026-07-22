// Offscreen logic harness for qml/guided/GuidedViewport.qml (Guided Reader, Task 9).
//
// Proves the viewport keeps the ORIGINAL page images intact (no cropped-panel
// substitution), treats a two-page spread as ONE wide coordinate space, exposes a
// usable normalized viewport centre, and reports manual interruption. Pixels are
// Hemanth's eyes-on; this pins the load-bearing logic only.
//
// House rules: define pass()/fail(); print the sentinel and Qt.exit(0/1). NEVER throw
// (an uncaught throw hangs the offscreen process instead of failing it).

import QtQuick
import QtQuick.Window
import "../qml/guided"

Window {
    id: win
    visible: true
    width: 900
    height: 700

    function pass(msg) { console.log(msg); Qt.exit(0) }
    function fail(msg) { console.log("GUIDED_VIEWPORT_FAIL: " + msg); Qt.exit(1) }

    property url leftUrl: Qt.resolvedUrl("../assets/addon-logos/marvel.png")
    property url rightUrl: Qt.resolvedUrl("../assets/addon-logos/dc.png")

    property int interruptCount: 0
    property int lastReason: -1

    GuidedViewport {
        id: view
        width: 800
        height: 600
        canvas: ({ kind: "spread",
                   files: [win.leftUrl, win.rightUrl],
                   sourceWidths: [400, 400],
                   sourceHeights: [600, 600] })
        cameraRect: Qt.rect(0.5, 0.0, 0.5, 1.0)
        transitionMs: 350

        onManualInterrupted: (reason, center) => {
            win.interruptCount += 1
            win.lastReason = reason
        }

        onTextureReady: {
            // 1. Both original spread pages remain as their own Image items.
            if (sourceItemCount !== 2) return win.fail("spread must retain two original Images, saw " + sourceItemCount)
            // 2. The spread is one wide canvas: 400 + 400 = 800 source-px wide.
            if (combinedCanvasWidth !== 800) return win.fail("spread is one wide coordinate space, saw " + combinedCanvasWidth)
            // 3. We never fabricate cropped-panel images.
            if (cropItemCount !== 0) return win.fail("no panel crop substitution, saw " + cropItemCount)
            // 4. Viewport centre tracks the camera rect centre (right-half spread => x=0.75, y=0.5).
            var c = view.viewportCenterNormalized()
            if (Math.abs(c.x - 0.75) > 0.001 || Math.abs(c.y - 0.5) > 0.001)
                return win.fail("viewport centre must equal camera-rect centre, saw " + c.x + "," + c.y)
            // 5. Manual interruption reports the input class and current centre.
            view.reportInterruption(1)
            if (win.interruptCount !== 1 || win.lastReason !== 1)
                return win.fail("manual interruption must emit reason=1 exactly once, saw " + win.interruptCount + "/" + win.lastReason)
            win.pass("GUIDED_VIEWPORT_OK")
        }
    }

    // Safety net: if textures never load (bad asset path / component error), fail loudly
    // instead of the offscreen process hanging until the runner's own timeout.
    Timer {
        interval: 8000
        running: true
        repeat: false
        onTriggered: win.fail("textureReady never fired within 8s (component load or image path error)")
    }
}
