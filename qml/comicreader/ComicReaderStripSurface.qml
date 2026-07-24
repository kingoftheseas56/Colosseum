// ComicReaderStripSurface — the Long Strip reading surface (Task 10).
//
// The manga-default vertical reader: one continuously-scrolling column of full-bleed pages on
// black. It is a THIN painter over the Task-7 backend — "QML paints, C++ decides":
//
//   * GEOMETRY IS AUTHORITATIVE. A ListView is bound to `core.stripModel` (the Task-6
//     ComicReaderStripModel, roles pageIndex/top/displayWidth/displayHeight/ready/errorCode). Each
//     delegate takes its height/width from the MODEL roles, never from the loaded Image's implicit
//     size — so a page decoding to a taller-than-estimated size never reflows/jerks the column.
//   * VIRTUALIZED. ListView keeps only near-viewport delegates (modest cacheBuffer); on scroll the
//     surface reports the viewport to core.setStripViewport(top,height) + setStripViewportWidth(w)
//     THROTTLED to at most once per frame (a 16ms coalescing timer, not one call per contentY tick).
//   * ANTI-JUMP. core.stripCompensation(delta) — emitted when a page above the fold decodes to a new
//     height — is added straight to contentY so the read position doesn't shift under the reader.
//   * SMOOTH WHEEL. The family's float accumulator (Tankoban-Max / QTGW SmoothScrollArea,
//     comic_reader.py ~1371-1439): ~100px/notch intake into a bounded backlog, ~0.38 drain per 16ms
//     tick capped by a max step, sub-pixel float contentY. THIS is the reading feel.
//   * PER-PAGE FAILURE. core.pageFailed(page,code) shows a typed placard on THAT page's delegate
//     only (missing / decode / unsupported); the rest of the column keeps reading.
//
// INJECTABLE + GUARDED. `core` is injected by the shell (its ComicReaderCore seam). Every `core.`
// use is guarded, so the surface also survives the shell's Task-9 fake (which has no imageUrl /
// stripModel / setStripViewport) — it simply renders nothing until a real core is present.
//
// STATE-MUTATING SIGNALS ARE GATED. pageInView/scrolled/manualNavigation fire ONLY on genuine user
// scroll (a wheel gesture), never on construction, resume, or compensation — so mounting this
// surface never clobbers the shell's resumed page/fraction. The shell binds `resumeFraction` in and
// consumes pageInView/scrolled out.

import QtQuick

Item {
    id: root

    // ---- injectable seam + inputs (bound by the shell) ----
    property var core: null
    property bool active: true
    property bool rtl: false                 // strip is vertical; kept for parity / future affordances
    property real resumeFraction: 0          // initial scroll fraction (resume-before-first-paint)
    property int cacheScreens: 2             // cacheBuffer in viewport-heights (modest -> virtualized)

    // ---- outputs consumed by the shell / HUD (Task 11) ----
    signal pageInView(int page)              // 1-based page at the vertical center (user scroll only)
    signal visiblePages(var indices)         // 0-based indices currently on screen (user scroll only)
    signal scrolled(real fraction)           // 0..1 scroll fraction (user scroll only)
    signal manualNavigation()                // a user scroll gesture happened (HUD auto-hide, record)

    clip: true

    // full-bleed black stage — the page owns the screen
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- test/observation surface (harness reads these; also handy for the HUD later) ----
    property alias contentY: list.contentY
    readonly property int rowCount: list.count
    readonly property real contentHeight: list.contentHeight
    function itemAt(i) { return list.itemAtIndex(i) }
    function forceRelayout() { list.forceLayout() }

    // ---- internal flags ----
    property bool _programmatic: false       // suppress user-signal emission for resume/compensation
    property bool _draining: false           // the wheel drain is authoring contentY (don't resync smoothY)
    property bool _userInteracted: false     // a wheel gesture has occurred (gates user-signal emission)
    property bool _resumeApplied: false

    // Decode-refresh dependency. The C++ provider returns a NULL image for a not-yet-decoded page
    // and imageUrl() embeds a per-page rev that bumps on pageReady — but imageUrl() reading that rev
    // is a C++-side read QML CANNOT see, so an Image `source` bound only to {core,page} would never
    // re-request after the decode lands (page stays blank until the delegate is recreated). This
    // counter, bumped on every pageReady and folded into each `source` via the comma-sequence trick,
    // is the reactive dependency that re-drives the binding so the fresh (?rev=N) url is re-fetched.
    property int readyRev: 0

    // ================= per-page failure tracking =================
    property var failedPages: ({})           // 0-based page -> code string
    property int failedRev: 0                 // bumped so delegates re-evaluate _failText()
    function _markFailed(page, code) {
        failedPages[page] = String(code)
        failedRev += 1
    }
    function _clearFailed(page) {
        if (failedPages[page] !== undefined) {
            delete failedPages[page]
            failedRev += 1
        }
    }
    function _failText(page, errorCode) {
        var code = failedPages[page]
        if (code === undefined || code === "") {
            var e = Number(errorCode) || 0     // model ErrorCodeRole int: 1 missing / 2 decode / 3 unsupported
            code = e === 1 ? "missing_file" : e === 2 ? "decode_failed" : e === 3 ? "unsupported_image" : ""
        }
        switch (code) {
        case "missing_file":      return "Page missing"
        case "decode_failed":     return "Couldn't decode"
        case "unsupported_image": return "Unsupported format"
        default:                  return ""
        }
    }

    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageFailed(page, code) { root._markFailed(page, code) }
        // decode landed: clear any failure AND bump readyRev so the page's `source` re-requests the
        // freshly-decoded pixels (the ?rev= url changed C++-side, invisibly to QML).
        function onPageReady(page)        { root._clearFailed(page); root.readyRev += 1 }
        function onStripCompensation(delta) { root._applyCompensation(delta) }
    }

    function _applyCompensation(delta) {
        // a page above the fold changed height — shift our scroll position by the same delta so the
        // read position holds. Programmatic (never a user gesture).
        _programmatic = true
        list.contentY = list.contentY + delta
        _smoothY = list.contentY
        _programmatic = false
    }

    // ================= the strip =================
    ListView {
        id: list
        anchors.fill: parent
        model: root.core ? root.core.stripModel : null
        orientation: ListView.Vertical
        cacheBuffer: Math.round(Math.max(1, root.height) * root.cacheScreens)
        boundsBehavior: Flickable.StopAtBounds
        interactive: false                    // wheel drives the smooth accumulator; drag/flick is Task 11
        pixelAligned: false                   // preserve the sub-pixel float feel

        delegate: Item {
            id: del
            width: ListView.view ? ListView.view.width : root.width
            height: model.displayHeight > 0 ? model.displayHeight : 0   // MODEL-authoritative geometry

            readonly property string errorText: (root.failedRev, root._failText(model.pageIndex, model.errorCode))
            readonly property bool hasError: errorText.length > 0
            readonly property alias imageSource: pageImg.source   // exposed so the decode-refresh test can see it re-evaluate

            // the page image — geometry comes from the delegate (model), NOT the image's implicit size
            Image {
                id: pageImg
                anchors.horizontalCenter: parent.horizontalCenter
                y: 0
                width: model.displayWidth > 0 ? model.displayWidth : parent.width
                height: parent.height
                visible: !del.hasError
                // readyRev folded in so the binding re-runs (and re-fetches the bumped ?rev= url) on decode
                source: (root.readyRev, (root.core && root.core.imageUrl) ? root.core.imageUrl(model.pageIndex) : "")
                asynchronous: true
                cache: false                  // the ?rev= in the url busts QML's cache on redecode
                retainWhileLoading: true      // hold the current pixels through a redecode (no flash)
                fillMode: Image.PreserveAspectFit
            }

            // typed error placard — this page only; the rest of the column keeps reading
            Rectangle {
                anchors.centerIn: parent
                visible: del.hasError
                width: Math.min(parent.width * 0.72, 360)
                height: 92
                radius: 10
                color: "#141019"
                border.color: "#2a2334"
                border.width: 1
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: del.errorText
                        color: "#ff8a8a"
                        font.pixelSize: 14
                        font.family: "Segoe UI"
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Page " + (model.pageIndex + 1)
                        color: "#9a99a5"
                        font.pixelSize: 11
                        font.family: "Segoe UI"
                    }
                }
            }
        }

        onContentYChanged: {
            root._scheduleReport()
            if (!root._draining) root._smoothY = list.contentY   // resync on any external move
            if (!root._programmatic && root._userInteracted) root._emitUserScroll()
        }
        onWidthChanged: root._scheduleReport()
        onHeightChanged: root._scheduleReport()
        onContentHeightChanged: root._applyResumeFraction()
    }

    // ================= throttled viewport report (<= once per frame) =================
    property bool _reportPending: false
    property int _lastReportedWidth: -1
    Timer { id: reportTimer; interval: 16; repeat: false; onTriggered: root._flushViewportReport() }
    function _scheduleReport() {
        // Both surfaces are mounted at once against the SAME core; a HIDDEN strip must not drive the
        // shared decode pool / cache. Only the active strip reports its viewport.
        if (!active) return
        _reportPending = true
        if (!reportTimer.running) reportTimer.start()
    }
    function _flushViewportReport() {
        _reportPending = false
        if (!active || !core) return
        if (core.setStripViewport) core.setStripViewport(list.contentY, list.height)
        if (core.setStripViewportWidth && list.width !== _lastReportedWidth) {
            // A width change (fullscreen, window resize) rescales the page column. The backend does this
            // IN PLACE (dataChanged, not a model reset), so the ListView keeps its delegates and its
            // contentY and simply reflows — no blink, no jump to page 1. The whole column scaled by
            // `ratio`, so scale the (preserved) scroll by the same factor to hold the read position.
            // forceLayout settles the new contentHeight first so the scaled value isn't clamped short.
            // Skip the first report (_lastReportedWidth < 0): initial layout, resume owns the position.
            var ratio = (_lastReportedWidth > 0) ? (list.width / _lastReportedWidth) : 1
            _lastReportedWidth = list.width
            core.setStripViewportWidth(list.width)
            if (ratio > 0 && ratio !== 1 && list.contentY > 0) {
                _programmatic = true
                list.forceLayout()
                list.contentY = list.contentY * ratio
                _smoothY = list.contentY
                _programmatic = false
            }
        }
    }
    // becoming active (mode switched back to strip) — push the current viewport once so the backend
    // resumes decoding the right window (geometry changes while hidden were intentionally ignored).
    onActiveChanged: if (active) _scheduleReport()

    // ================= smooth-wheel float accumulator (Tankoban-Max / QTGW SmoothScrollArea) =================
    property real _pendingWheelPx: 0         // bounded backlog of intake still to drain
    property real _smoothY: 0                // float sub-pixel scroll position (contentY is round(this))
    Timer { id: wheelTimer; interval: 16; repeat: true; onTriggered: root._drainWheel() }

    // intake ~100px per notch into the bounded backlog (mirror comic_reader.py wheelEvent)
    function _intakeWheel(angleY, pixelY) {
        var dy = pixelY
        if (dy === 0) dy = angleY * (100.0 / 120.0)
        if (dy === 0) return
        _userInteracted = true
        manualNavigation()
        var vpH = Math.max(1, height)
        var maxInput = Math.max(1200.0, vpH * 3)
        var step = Math.max(-maxInput, Math.min(maxInput, -dy))   // wheel-down (angle<0) -> +contentY
        var cap = Math.max(2400.0, vpH * 8)
        _pendingWheelPx = Math.max(-cap, Math.min(cap, _pendingWheelPx + step))
        if (!wheelTimer.running) wheelTimer.start()
    }

    // drain ~0.38 of the backlog per 16ms tick, capped by max step (mirror comic_reader.py _drain_wheel)
    function _drainWheel() {
        if (Math.abs(_pendingWheelPx) < 0.75) {
            _pendingWheelPx = 0
            wheelTimer.stop()
            return
        }
        var vpH = Math.max(1, height)
        var maxStep = Math.max(70.0, vpH * 0.22)
        var take = _pendingWheelPx * 0.38
        take = Math.max(-maxStep, Math.min(maxStep, take))
        if (Math.abs(take) < 2.0) take = _pendingWheelPx      // finish off the last sub-2px sliver
        _pendingWheelPx -= take
        _smoothY += take
        var maxY = Math.max(0, list.contentHeight - list.height)
        _smoothY = Math.max(0, Math.min(maxY, _smoothY))
        var newY = Math.round(_smoothY)
        if (newY !== list.contentY) {
            _draining = true
            list.contentY = newY
            _draining = false
        }
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) { root._intakeWheel(event.angleDelta.y, event.pixelDelta.y) }
    }

    // ================= resume + user-scroll reporting =================
    function _applyResumeFraction() {
        if (_resumeApplied) return
        var span = list.contentHeight - list.height
        if (span <= 0) return                 // content not laid out yet / fits — nothing to resume to
        _resumeApplied = true
        _programmatic = true
        list.contentY = Math.max(0, Math.min(span, resumeFraction * span))
        _smoothY = list.contentY
        _programmatic = false
    }
    onResumeFractionChanged: _applyResumeFraction()

    function _emitUserScroll() {
        if (list.count <= 0) return
        var span = list.contentHeight - list.height
        var frac = span > 0 ? (list.contentY / span) : 0
        root.scrolled(Math.max(0, Math.min(1, frac)))
        var idx = list.indexAt(list.width / 2, list.contentY + list.height / 2)
        if (idx >= 0) root.pageInView(idx + 1)     // 1-based (shell currentPage scale)
        var lo = list.indexAt(list.width / 2, list.contentY + 1)
        var hi = list.indexAt(list.width / 2, list.contentY + list.height - 1)
        if (lo >= 0 && hi >= 0) {
            var arr = []
            for (var i = lo; i <= hi; i++) arr.push(i)
            root.visiblePages(arr)
        }
    }

    Component.onCompleted: Qt.callLater(_applyResumeFraction)
}
