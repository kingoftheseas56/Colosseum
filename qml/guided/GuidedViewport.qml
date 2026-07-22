// GuidedViewport — the intact-canvas surface for the Guided Reader (fifth style).
//
// Plainly: it shows the comic page(s) at full fidelity and moves a "camera" — a
// normalized rectangle in the canvas's own 0..1 space — smoothly over them. It NEVER
// crops or substitutes panel images; a two-page spread is treated as ONE wide canvas.
// It only PAINTS and REPORTS input; the GuidedCameraController decides where the camera
// goes and MangaReader decides what a manual gesture means. (COLOSSEUM_DOCTRINE: QML
// paints, C++ decides.)
//
// Reason codes on manualInterrupted match GuidedCameraController.InterruptionReason:
//   0 = Wheel, 1 = Drag, 2 = Pinch  (Scrub/Navigation are raised by MangaReader, not here).

import QtQuick

Item {
    id: root

    // --- inputs (bound by MangaReader) ---
    property var canvas: ({})              // { kind, files:[url], sourceWidths:[px], sourceHeights:[px] }
    property rect cameraRect: Qt.rect(0, 0, 1, 1)   // normalized region of the combined canvas to frame
    property int transitionMs: 350
    property int stopAnimationGeneration: 0          // bump to snap (freeze) instead of animate

    // --- outputs ---
    signal textureReady()
    signal manualInterrupted(int reason, point center)

    // --- observable geometry (asserted by the logic harness) ---
    readonly property var _files: (canvas && canvas.files) ? canvas.files : []
    readonly property var _widths: (canvas && canvas.sourceWidths) ? canvas.sourceWidths : []
    readonly property var _heights: (canvas && canvas.sourceHeights) ? canvas.sourceHeights : []
    readonly property int sourceItemCount: pageRepeater.count
    readonly property int cropItemCount: 0           // we never fabricate cropped-panel images
    readonly property real combinedCanvasWidth: _sum(_widths, _files.length, 800)
    readonly property real combinedCanvasHeight: _max(_heights, _files.length, 1200)

    clip: true

    // The normalized point currently at the viewport centre = the camera-rect centre.
    function viewportCenterNormalized() {
        return Qt.point(cameraRect.x + cameraRect.width / 2,
                        cameraRect.y + cameraRect.height / 2)
    }

    // Single seam for raising a manual interruption (also called by MangaReader's own
    // interrupt sources, e.g. the scrub bar). Reason is a GuidedCameraController code.
    function reportInterruption(reason) {
        root.manualInterrupted(reason, root.viewportCenterNormalized())
    }

    function _sum(arr, n, fallback) {
        if (!arr || arr.length < n || n <= 0) return fallback
        var t = 0
        for (var i = 0; i < n; ++i) t += arr[i]
        return t
    }
    function _max(arr, n, fallback) {
        if (!arr || arr.length < n || n <= 0) return fallback
        var m = 0
        for (var i = 0; i < n; ++i) if (arr[i] > m) m = arr[i]
        return m > 0 ? m : fallback
    }
    // Normalized x-offset of source page `i` within the combined canvas (physical order).
    function _offsetX(i) {
        var t = 0
        for (var k = 0; k < i; ++k) t += (_widths[k] || 0)
        return combinedCanvasWidth > 0 ? t / combinedCanvasWidth : 0
    }

    // --- camera freeze: on a generation bump, disable animation for one turn so an
    //     interruption is stationary by the next frame, then resume gliding. ---
    property bool _animate: true
    onStopAnimationGenerationChanged: {
        _animate = false
        _resumeAnim.restart()
    }
    Timer { id: _resumeAnim; interval: 0; repeat: false; onTriggered: root._animate = true }

    // --- texture-ready bookkeeping ---
    property int _loaded: 0
    property bool _emitted: false
    onCanvasChanged: { _loaded = 0; _emitted = false }
    function _noteLoaded() {
        _loaded += 1
        if (!_emitted && _files.length > 0 && _loaded >= _files.length) {
            _emitted = true
            root.textureReady()
        }
    }

    // The whole canvas lives in one coordinate space; the camera scales/translates it so
    // that `cameraRect` fills the viewport. Width and height follow the rect (the approved
    // guided-v1 framing); each page Image uses PreserveAspectFit so no source ever stretches.
    Item {
        id: sourceCanvas
        width: root.width / Math.max(0.0001, root.cameraRect.width)
        height: root.height / Math.max(0.0001, root.cameraRect.height)
        x: root.width / 2 - (root.cameraRect.x + root.cameraRect.width / 2) * width
        y: root.height / 2 - (root.cameraRect.y + root.cameraRect.height / 2) * height

        Behavior on x      { enabled: root._animate; NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Behavior on y      { enabled: root._animate; NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Behavior on width  { enabled: root._animate; NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }
        Behavior on height { enabled: root._animate; NumberAnimation { duration: root.transitionMs; easing.type: Easing.InOutCubic } }

        Repeater {
            id: pageRepeater
            model: root._files
            delegate: Image {
                required property int index
                required property var modelData
                source: modelData
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                // Lay pages side by side in the physical order supplied by `canvas`,
                // each occupying its true share of the combined canvas.
                x: root._offsetX(index) * sourceCanvas.width
                y: 0
                width: ((root._widths[index] || 0) / Math.max(1, root.combinedCanvasWidth)) * sourceCanvas.width
                height: ((root._heights[index] || 0) / Math.max(1, root.combinedCanvasHeight)) * sourceCanvas.height
                onStatusChanged: if (status === Image.Ready || status === Image.Error) root._noteLoaded()
            }
        }
    }

    // --- manual-input reporting (report only; MangaReader/controller decide behaviour) ---
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => root.reportInterruption(0)   // Wheel
    }
    DragHandler {
        target: null
        onActiveChanged: if (active) root.reportInterruption(1)   // Drag
    }
    PinchHandler {
        target: null
        onActiveChanged: if (active) root.reportInterruption(2)   // Pinch
    }
}
