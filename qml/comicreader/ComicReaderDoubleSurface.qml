// ComicReaderDoubleSurface — the direction-aware Double Page reading surface (Task 10).
//
// Renders the CANONICAL unit for the current page (from the Task-7 backend, never re-derived here):
// core.unitForPage(currentPage-1) -> {rightIndex, leftIndex(-1 absent), spread, coverAlone}.
//
//   * spread / coverAlone / single (leftIndex<0) -> ONE full-viewport-width image
//     (core.imageUrl(rightIndex)); an intact page, NEVER a fabricated crop.
//   * a real pair -> TWO images side by side, and the PHYSICAL x-order flips with direction:
//       RTL (manga)  — the rightIndex page sits on the physical RIGHT, leftIndex on the LEFT.
//       LTR (western)— mirrored (rightIndex page on the physical LEFT).
//     (Mirrors QTGW DoublePageCanvas._draw_pair, which swaps the two images when not rtl.)
//   * GUTTER SHADOW — a soft dark vertical gradient over the spine, strength from `gutterStrength`
//     (presets 0 / 0.22 / 0.35 / 0.55). Only for a real pair, never a spread/single.
//   * ZOOM 100–260% (20% steps) + PAN. `zoomPercent` widens the spread; when zoomed, pan slides it;
//     pan clamps to the zoomed bounds; zoom+pan RESET when the unit changes.
//     (Matches QTGW DoublePageCanvas: set_zoom clamps 1.0–2.6 and resets pan.)
//
// maxSeen PAIR-ANCHOR CONTRACT (shell Task 9, onCurrentPageChanged): in double mode `currentPage`
// is the pair ANCHOR, and a pair-terminated chapter would never reach `finished` from the anchor
// alone. So every time a unit is shown, the surface emits unitShown(highestPage) with the
// reading-HIGHEST page of the unit — max(rightIndex,leftIndex)+1 in the shell's 1-based page scale.
// The shell folds it into maxSeen, so completion advances to the unit's highest page. (This is the
// Comic Reader equivalent of MangaReader.qml bumpSeen() folding in the pair partner index.)
//
// INJECTABLE + GUARDED. `core` is injected by the shell; every `core.` use is guarded so the
// surface also survives the shell's Task-9 fake (no imageUrl). Unit computation + all side effects
// (setVisible, unitShown) are gated behind `active`, so an INVISIBLE double surface (long_strip
// mode) never touches the backend or the shell's maxSeen.

import QtQuick

Item {
    id: root

    // ---- injectable seam + inputs (bound by the shell) ----
    property var core: null
    property bool active: false
    property int currentPage: 1              // 1-based pair ANCHOR
    property bool rtl: false
    property real gutterStrength: 0.35       // presets 0 / 0.22 / 0.35 / 0.55

    // ---- zoom/pan (surface-owned; reset on unit change) ----
    property int zoomPercent: 100            // self-managed, clamped to [100,260]
    property real panX: 0
    property real panY: 0

    // ---- outputs consumed by the shell / HUD (Task 11) ----
    signal unitShown(int highestPage)        // 1-based reading-highest page in the shown unit (maxSeen)
    signal nextRequested()                   // advance one UNIT (shell/core own the canonical walk)
    signal previousRequested()
    signal manualNavigation()

    // Decode-refresh dependency (same reason as the strip): imageUrl()'s ?rev= bumps C++-side on
    // pageReady, invisibly to QML, so an Image `source` bound only to {core,index} would never
    // re-request after the decode lands — every freshly-navigated unit would stay blank until you
    // navigate away and back. readyRev is bumped on pageReady and folded into both `source` bindings.
    property int readyRev: 0
    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageReady(page) { root.readyRev += 1 }
    }

    clip: true
    Rectangle { anchors.fill: parent; color: "#000000" }   // full-bleed black stage

    // ================= the canonical unit =================
    readonly property var _emptyUnit: ({ rightIndex: -1, leftIndex: -1, spread: false, coverAlone: false })
    function _normUnit(u) {
        if (!u) return _emptyUnit
        return { rightIndex: (u.rightIndex !== undefined ? u.rightIndex : -1),
                 leftIndex:  (u.leftIndex  !== undefined ? u.leftIndex  : -1),
                 spread:     !!u.spread,
                 coverAlone: !!u.coverAlone }
    }
    readonly property var unit: (active && core && core.unitForPage)
        ? _normUnit(core.unitForPage(Math.max(0, currentPage - 1)))
        : _emptyUnit

    readonly property bool isPair: unit.leftIndex >= 0 && unit.rightIndex >= 0 && !unit.spread && !unit.coverAlone
    readonly property bool isSingle: !isPair && (unit.rightIndex >= 0 || unit.leftIndex >= 0)
    readonly property int imageCount: isPair ? 2 : (isSingle ? 1 : 0)

    // reading-HIGHEST page of the unit, 1-based (the shell's maxSeen scale)
    readonly property int unitHighestPage: {
        var hi = Math.max(unit.rightIndex, unit.leftIndex)
        return hi >= 0 ? hi + 1 : Math.max(1, currentPage)
    }

    // ================= zoom / pan geometry =================
    readonly property int clampedZoom: Math.max(100, Math.min(260, zoomPercent))
    readonly property real zoomFactor: clampedZoom / 100.0
    readonly property real _contentW: width * zoomFactor
    readonly property real _halfW: _contentW / 2
    readonly property real panXMax: Math.max(0, _contentW - width)
    readonly property real _maxImgH: Math.max(rightImg.visible ? rightImg.height : 0,
                                              leftImg.visible ? leftImg.height : 0)
    readonly property real panYMax: Math.max(0, _maxImgH - height)

    function setZoom(pct) {
        zoomPercent = Math.max(100, Math.min(260, Math.round(pct)))
        panX = 0; panY = 0
    }
    function zoomIn()  { setZoom(clampedZoom + 20) }
    function zoomOut() { setZoom(clampedZoom - 20) }
    function panBy(dx, dy) {
        panX = Math.max(0, Math.min(panXMax, panX + dx))
        panY = Math.max(0, Math.min(panYMax, panY + dy))
    }
    function next()     { manualNavigation(); nextRequested() }
    function previous() { manualNavigation(); previousRequested() }

    // ================= physical x-order (viewport coords) — for direction assert + HUD =================
    // rightIndex page: physical RIGHT in RTL, physical LEFT in LTR; leftIndex page mirrors it.
    readonly property real rightIndexX: content.x + (isPair ? (rtl ? _halfW : 0) : 0)
    readonly property real leftIndexX:  content.x + (isPair ? (rtl ? 0 : _halfW) : 0)
    readonly property real singleImageWidth: rightImg.width
    readonly property alias gutterShadowItem: gutterShadow
    readonly property bool gutterVisible: gutterShadow.visible
    readonly property alias rightSource: rightImg.source   // exposed so the decode-refresh test sees the source re-evaluate
    readonly property alias leftSource: leftImg.source

    // ================= unit lifecycle: reset zoom/pan, pin pages, drive maxSeen =================
    // Driven off the RELIABLE int/bool change signals (currentPage/active), computing the unit FRESH
    // here — NOT off the `unit`/`unitHighestPage` var-property bindings, whose re-eval ordering vs the
    // handler is not guaranteed (a var binding can still read stale when the handler pulls it).
    function _currentUnit() {
        return (active && core && core.unitForPage)
            ? _normUnit(core.unitForPage(Math.max(0, currentPage - 1))) : _emptyUnit
    }
    function _highestOf(u) {
        var hi = Math.max(u.rightIndex, u.leftIndex)
        return hi >= 0 ? hi + 1 : Math.max(1, currentPage)
    }
    function _onUnitShown() {
        zoomPercent = 100; panX = 0; panY = 0          // a new unit opens at 100% / top-left
        if (!active) return
        var u = _currentUnit()
        if (core && core.setVisible) {
            var vis = []
            if (u.rightIndex >= 0) vis.push(u.rightIndex)
            if (u.leftIndex >= 0) vis.push(u.leftIndex)
            if (vis.length) core.setVisible(vis)
        }
        unitShown(_highestOf(u))                        // fold the unit's highest page into the shell's maxSeen
    }
    onCurrentPageChanged: _onUnitShown()               // a page turn -> a new unit
    onActiveChanged: _onUnitShown()                    // mode switches into/out of double

    // ================= the spread =================
    Item {
        id: content
        width: root._contentW
        height: root.height
        x: -root.panX                                  // horizontal pan slides the zoomed content

        // rightIndex page (also the single/spread image, full width)
        Image {
            id: rightImg
            visible: root.isPair || root.isSingle
            source: (root.readyRev, (visible && root.core && root.core.imageUrl && root.unit.rightIndex >= 0)
                    ? root.core.imageUrl(root.unit.rightIndex) : "")
            asynchronous: true
            cache: false
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            width: root.isPair ? root._halfW : root._contentW
            height: implicitWidth > 0 ? width * implicitHeight / implicitWidth : root.height
            x: root.isPair ? (root.rtl ? root._halfW : 0) : 0
            y: -root.panY
        }

        // leftIndex page (only for a real pair)
        Image {
            id: leftImg
            visible: root.isPair
            source: (root.readyRev, (visible && root.core && root.core.imageUrl && root.unit.leftIndex >= 0)
                    ? root.core.imageUrl(root.unit.leftIndex) : "")
            asynchronous: true
            cache: false
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            width: root._halfW
            height: implicitWidth > 0 ? width * implicitHeight / implicitWidth : root.height
            x: root.rtl ? 0 : root._halfW
            y: -root.panY
        }

        // gutter shadow — soft spine, only for a real pair
        Rectangle {
            id: gutterShadow
            visible: root.isPair && root.gutterStrength > 0
            width: 18
            height: root.height
            x: root._halfW - width / 2
            y: 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.00; color: Qt.rgba(0, 0, 0, 0.10 * root.gutterStrength) }
                GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.34 * root.gutterStrength) }
                GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.34 * root.gutterStrength) }
                GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.10 * root.gutterStrength) }
            }
        }
    }
}
