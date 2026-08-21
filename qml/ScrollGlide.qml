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

    function _maxY() {
        if (!glide.flick)
            return 0
        return Math.max(0, glide.flick.contentHeight - glide.flick.height)
    }

    function cancelGlide() {
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
            glide.cancelGlide()
            return
        }

        if (Math.abs(glide._pendingPx) < glide.settleEpsilonPx) {
            var settledY = Math.max(
                0,
                Math.min(glide._maxY(), glide._smoothY + glide._pendingPx)
            )
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
            glide.cancelGlide()
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

        if (y <= 0 || y >= maxY) {
            y = Math.max(0, Math.min(maxY, y))

            // Never carry hidden momentum beyond a hard boundary.
            glide._pendingPx = 0
        } else {
            glide._pendingPx -= take
        }

        glide._smoothY = y

        glide._draining = true
        glide.flick.contentY = y
        glide._draining = false

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
                glide.cancelGlide()
        }

        // Scrollbar / seek / other direct repositioning must also rebase.
        function onContentYChanged() {
            if (!glide.flick || glide._draining)
                return

            if (!scrollDrain.running) {
                glide._smoothY = glide.flick.contentY
                return
            }

            if (Math.abs(glide.flick.contentY - glide._smoothY)
                    > glide.externalRebaseTolerancePx) {
                glide.cancelGlide()
            }
        }
    }

    // A reused component must not carry a backlog from its previous Flickable.
    onFlickChanged: glide.cancelGlide()

    WheelHandler {
        target: glide.flick
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.NoModifier

        onWheel: function(e) {
            if (!glide.flick)
                return

            // Trackpads already report pixels. Do not multiply them by speed.
            var dy = e.pixelDelta.y

            // Mouse wheel fallback.
            if (dy === 0)
                dy = e.angleDelta.y * glide.speed

            if (dy === 0)
                return

            // Wheel-down is negative input delta and must increase contentY.
            glide.smoothScrollBy(-dy)
            e.accepted = true
        }
    }

    // Existing vertical GridView callers use these page-step entry points. They
    // remain additive wrappers over the same wheel backlog, not a second motion law.
    function _animateTo(absoluteY) {
        if (!glide.flick)
            return
        var maxY = glide._maxY()
        var target = Math.max(0, Math.min(maxY, absoluteY))
        // Absolute commands replace any wheel target already in flight.
        glide.cancelGlide()
        glide.smoothScrollBy(target - glide.flick.contentY)
    }
    function pageUp() { if (glide.flick) glide._animateTo(glide.flick.contentY - glide.flick.height * 0.85) }
    function pageDown() { if (glide.flick) glide._animateTo(glide.flick.contentY + glide.flick.height * 0.85) }
    function toTop() { glide._animateTo(0) }
    function toBottom() { glide._animateTo(glide._maxY()) }
}
