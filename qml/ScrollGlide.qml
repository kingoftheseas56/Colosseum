// ScrollGlide — shared frame-synchronised wheel glide.
//
// This deliberately follows ComicReaderStripSurface's Long Strip motion law:
//   * pixelDelta is already pixels and is never multiplied by mouse-wheel speed
//   * angleDelta falls back to 1.4 px / angle unit (~168 px per ordinary notch)
//   * input accumulates into a bounded backlog
//   * a FrameAnimation drains 38% of the remaining backlog per 60-Hz-equivalent frame
//   * frameTime compensates for refresh rate / ordinary frame variation
//   * contentY remains floating-point / sub-pixel
//   * external repositioning cancels stale wheel backlog
//
// Touch/drag remains Flickable-owned. ScrollGlide only owns wheel backlog.
import QtQuick
import "ScrollGlideRegistry.js" as Registry

Item {
    id: glide

    property Flickable flick: null

    // Existing public tuning contract: used only for angleDelta fallback.
    // 120 angle units * 1.4 = ~168 px per ordinary wheel notch.
    property real speed: 1.4

    // Reader-parity motion constants.
    property real drainFraction: 0.38
    property real maxBacklogPx: 6000
    property real settleEpsilonPx: 0.75
    property real externalRebaseTolerancePx: 1.5

    // Backlog still waiting to be presented.
    property real _pendingPx: 0

    // Authoritative floating scroll position while this component is draining.
    property real _smoothY: 0

    // FrameAnimation's first tick after idle can report a short frameTime.
    // Treat the first tick as at least one ordinary frame so input starts
    // immediately instead of producing a visible dead beat.
    property bool _drainFresh: false

    // Suppresses our own contentY write from being mistaken for an external move.
    property bool _draining: false

    // Diagnostic (COLOSSEUM_SCROLL_PROBE=1, native/ScrollProbe.h): log wheel input received,
    // every drained frame, and every backlog discarded with its reason. Off = no work.
    readonly property bool _probe: typeof ScrollProbeEnabled !== "undefined" && ScrollProbeEnabled === true
    function _plog(msg) {
        if (!glide._probe)
            return
        var name = glide.flick ? (glide.flick.objectName || String(glide.flick)) : "none"
        console.info("SCROLL_PROBE glide t=" + Date.now() + " flick=" + name + " " + msg)
    }

    function _maxY() {
        if (!glide.flick)
            return 0
        var minimum = glide.flick.originY - glide.flick.topMargin
        var end = glide.flick.originY + glide.flick.contentHeight
                - glide.flick.height + glide.flick.bottomMargin
        return Math.max(minimum, end)
    }

    function _minY() {
        if (!glide.flick)
            return 0
        return glide.flick.originY - glide.flick.topMargin
    }

    function cancelGlide(reason) {
        glide._keyboardGlide = false
        if (glide._probe && glide._pendingPx !== 0)
            glide._plog("cancel reason=" + (reason || "api") + " lostPx=" + glide._pendingPx.toFixed(1)
                        + " y=" + (glide.flick ? glide.flick.contentY.toFixed(1) : "-")
                        + " smoothY=" + glide._smoothY.toFixed(1))
        scrollDrain.running = false
        glide._pendingPx = 0

        if (glide.flick)
            glide._smoothY = glide.flick.contentY

        glide._drainFresh = false
    }

    // Public programmatic seam for wheel-equivalent smooth motion.
    // Positive px moves downward; negative px moves upward.
    function smoothScrollBy(px) {
        if (!glide.flick || px === 0)
            return

        if (!scrollDrain.running) {
            glide._smoothY = glide.flick.contentY
            glide._drainFresh = true
        }

        glide._pendingPx = Math.max(
            -glide.maxBacklogPx,
            Math.min(
                glide.maxBacklogPx,
                glide._pendingPx + px
            )
        )

        if (!scrollDrain.running)
            scrollDrain.running = true
    }

    // One presented-frame-equivalent drain.
    //
    // frameTimeSeconds is injected by FrameAnimation in production. Keeping it
    // as an argument also gives the deterministic harness a synchronous seam.
    function _drainWheel(frameTimeSeconds) {
        if (!glide.flick) {
            glide.cancelGlide("noflick")
            return
        }

        if (Math.abs(glide._pendingPx) < glide.settleEpsilonPx) {
            var settledY = Math.max(
                glide._minY(),
                Math.min(glide._maxY(), glide._smoothY + glide._pendingPx)
            )
            // Frame-time-weighted takes leave float residue (503.9999999999999 for 504):
            // settle on the whole pixel so "the glide ended at Y" is an exact, waitable fact.
            if (Math.abs(settledY - Math.round(settledY)) < 1e-6)
                settledY = Math.round(settledY)
            glide._smoothY = settledY
            glide._draining = true
            glide.flick.contentY = settledY
            glide._draining = false
            glide._pendingPx = 0
            scrollDrain.running = false
            return
        }

        // Something other than this drain moved the Flickable between frames.
        // The user's/new owner's move wins and stale wheel momentum is discarded.
        if (Math.abs(glide.flick.contentY - glide._smoothY)
                > glide.externalRebaseTolerancePx) {
            glide.cancelGlide("drain-rebase")
            return
        }

        var dt = Number(frameTimeSeconds)
        if (!isFinite(dt) || dt <= 0)
            dt = 1.0 / 60.0

        // Compensate by elapsed presented-frame time, with bounds so an unusual
        // stall does not teleport several pages at once.
        var frames = Math.min(3.0, Math.max(0.25, dt * 60.0))

        if (glide._drainFresh) {
            frames = Math.max(1.0, frames)
            glide._drainFresh = false
        }

        var take = glide._pendingPx
                * (1.0 - Math.pow(1.0 - glide.drainFraction, frames))

        if (Math.abs(glide._pendingPx) <= 1.0)
            take = glide._pendingPx

        var maxY = glide._maxY()
        var y = glide._smoothY + take

        if (y <= glide._minY() || y >= maxY) {
            y = Math.max(glide._minY(), Math.min(maxY, y))

            // Never carry hidden momentum beyond a hard boundary.
            if (glide._probe && Math.abs(glide._pendingPx - take) > 1)
                glide._plog("cancel reason=bound lostPx=" + (glide._pendingPx - take).toFixed(1)
                            + " y=" + y.toFixed(1))
            glide._pendingPx = 0
        } else {
            glide._pendingPx -= take
        }

        glide._smoothY = y

        glide._draining = true
        glide.flick.contentY = y
        glide._draining = false

        if (glide._probe)
            glide._plog("frame dtMs=" + (dt * 1000).toFixed(1) + " take=" + take.toFixed(1)
                        + " y=" + y.toFixed(1) + " pending=" + glide._pendingPx.toFixed(1)
                        + " h=" + glide.flick.contentHeight.toFixed(0) + " vh=" + glide.flick.height.toFixed(0))

        if (glide._pendingPx === 0)
            scrollDrain.running = false
    }

    FrameAnimation {
        id: scrollDrain
        running: false
        onTriggered: glide._drainWheel(scrollDrain.frameTime)
    }

    Connections {
        target: glide.flick

        // A real Flickable-owned movement (touch drag/flick/etc.) takes
        // authority and cancels queued wheel momentum.
        function onMovingChanged() {
            if (glide.flick && glide.flick.moving && !glide._draining)
                glide.cancelGlide("moving")
        }

        // Scrollbar / seek / other direct repositioning must also rebase.
        function onContentYChanged() {
            if (!glide.flick || glide._draining)
                return

            if (glide._probe)
                glide._plog("external y=" + glide.flick.contentY.toFixed(1)
                            + " delta=" + (glide.flick.contentY - glide._smoothY).toFixed(1)
                            + " gliding=" + (scrollDrain.running ? 1 : 0))

            if (!scrollDrain.running) {
                glide._smoothY = glide.flick.contentY
                return
            }

            if (Math.abs(glide.flick.contentY - glide._smoothY)
                    > glide.externalRebaseTolerancePx) {
                glide.cancelGlide("external")
            }
        }
    }

    // A reused component must not carry a backlog from its previous Flickable.
    onFlickChanged: {
        glide.cancelGlide("flickChanged")
        glide._attachWheel()
    }
    Component.onCompleted: glide._attachWheel()
    Component.onDestruction: {
        Registry.unregister(glide)
        if (glide._wheel)
            glide._wheel.destroy()
    }

    // The wheel handler must live ON the Flickable. A pointer handler only sees events inside its
    // parent item, and ScrollGlide is usually a zero-size sibling of its Flickable: declared here,
    // the handler never saw a wheel and Qt's own Flickable wheel (~29 px/notch) ran instead
    // (SCROLL_PROBE recording, 2026-09-29). So it is created with the Flickable as its parent.
    property var _wheel: null
    function _attachWheel() {
        if (glide._wheel) {
            glide._wheel.destroy()
            glide._wheel = null
        }
        Registry.register(glide.flick, glide)
        if (glide.flick)
            glide._wheel = wheelComponent.createObject(glide.flick)
    }

    function _onWheel(e) {
        if (!glide.flick)
            return

        // Trackpads already report pixels. Do not multiply them by speed.
        var dy = e.pixelDelta.y

        // Mouse wheel fallback.
        if (dy === 0)
            dy = e.angleDelta.y * glide.speed

        if (glide._probe)
            glide._plog("wheel ad=" + e.angleDelta.y + " pd=" + e.pixelDelta.y + " dy=" + dy
                        + " dev=" + (e.device ? e.device.type : "-")
                        + " y=" + glide.flick.contentY.toFixed(1) + " pendingBefore=" + glide._pendingPx.toFixed(1))

        if (dy === 0)
            return

        // Already at the end in this direction with nothing queued: hand the wheel to the outer
        // scroller (a nested wall at its bottom passes it to the page), as browsers do.
        var y = glide.flick.contentY
        if ((dy < 0 && glide._pendingPx >= 0 && y >= glide._maxY() - 0.5)
                || (dy > 0 && glide._pendingPx <= 0 && y <= glide._minY() + 0.5)) {
            var outer = Registry.outerGlide(glide.flick)
            if (outer)
                outer._onWheel(e)
            return
        }

        // Wheel-down is negative input delta and must increase contentY.
        glide.smoothScrollBy(-dy)
        e.accepted = true
    }

    Component {
        id: wheelComponent
        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: function(e) { glide._onWheel(e) }
        }
    }

    // The keyboard navigator places contentY at its landing position at once (its visibility
    // checks need the final geometry); this replays that jump as a glide from where the page was.
    function glideFrom(fromY) {
        if (!glide.flick)
            return
        var target = glide.flick.contentY
        if (Math.abs(target - fromY) < 1)
            return
        glide.cancelGlide("keyboard")
        glide._draining = true
        glide.flick.contentY = fromY
        glide._draining = false
        glide._smoothY = fromY
        glide.smoothScrollBy(target - fromY)
        glide._keyboardGlide = true
    }

    // Keyboard glide to an absolute position (WorldPage parks a focused row); settled like
    // glideFrom when the next key arrives.
    function glideTo(y) {
        if (!glide.flick)
            return
        var target = Math.max(glide._minY(), Math.min(glide._maxY(), y))
        glide.cancelGlide("keyboard")
        if (Math.abs(target - glide.flick.contentY) < 1)
            return
        glide.smoothScrollBy(target - glide.flick.contentY)
        glide._keyboardGlide = true
    }

    // Jump a keyboard glide to its end (the next key press needs the settled geometry).
    property bool _keyboardGlide: false
    function settleKeyboardGlide() {
        if (!glide._keyboardGlide)
            return
        glide._keyboardGlide = false
        if (!glide.flick || !scrollDrain.running)
            return
        var y = Math.max(glide._minY(), Math.min(glide._maxY(), glide._smoothY + glide._pendingPx))
        scrollDrain.running = false
        glide._pendingPx = 0
        glide._smoothY = y
        glide._draining = true
        glide.flick.contentY = y
        glide._draining = false
    }

    // Existing vertical GridView callers use these page-step entry points. They
    // remain additive wrappers over the same wheel backlog, not a second motion law.
    function _animateTo(absoluteY) {
        if (!glide.flick)
            return
        var maxY = glide._maxY()
        var target = Math.max(glide._minY(), Math.min(maxY, absoluteY))
        // Absolute commands replace any wheel target already in flight.
        glide.cancelGlide("absolute")
        glide.smoothScrollBy(target - glide.flick.contentY)
    }
    function pageUp() { if (glide.flick) glide._animateTo(glide.flick.contentY - glide.flick.height * 0.85) }
    function pageDown() { if (glide.flick) glide._animateTo(glide.flick.contentY + glide.flick.height * 0.85) }
    function toTop() { glide._animateTo(glide._minY()) }
    function toBottom() { glide._animateTo(glide._maxY()) }
}
