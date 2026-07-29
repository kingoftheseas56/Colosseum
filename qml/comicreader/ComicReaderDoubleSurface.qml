// ComicReaderDoubleSurface — the direction-aware Double Page reading surface (Task 10).
//
// Renders the CANONICAL unit for the current page (from the Task-7 backend, never re-derived here):
// core.unitForPage(currentPage-1) -> {rightIndex, leftIndex(-1 absent), spread, coverAlone}.
//
//   * spread / coverAlone / single (leftIndex<0) -> ONE full-viewport-width image
//     (core.imageUrl(rightIndex)); an intact page, NEVER a fabricated crop.
//   * ONE SCALE for the whole displayed unit (both lineage readers), so a pair of unevenly trimmed
//     scans keeps its true relative size, meets FLUSH at the spine, and centres vertically as one
//     block instead of hanging off the top. Natural size comes from the BACKEND's header geometry,
//     not the Image's implicitWidth — see the unitScale block for why that distinction is the fix.
//   * a real pair -> TWO images side by side, and the PHYSICAL x-order flips with direction:
//       RTL (manga)  — the rightIndex page sits on the physical RIGHT, leftIndex on the LEFT.
//       LTR (western)— mirrored (rightIndex page on the physical LEFT).
//     (Mirrors QTGW DoublePageCanvas._draw_pair, which swaps the two images when not rtl.)
//   * GUTTER SHADOW — a soft dark vertical gradient over the spine, strength from `gutterStrength`
//     (presets 0 / 0.22 / 0.35 / 0.55). Only for a real pair, never a spread/single.
//   * ZOOM 100–260% (20% steps) + PAN. `zoomPercent` widens the spread; when zoomed, pan slides it;
//     pan clamps to the zoomed bounds; a unit change RESETS PAN ONLY — zoom SURVIVES a page
//     turn, or a magnified volume would snap back to 100% on every turn (see _onUnitShown).
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

    // ---- zoom/pan (surface-owned; pan resets on a unit change, zoom persists across turns) ----
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
    // Entry/pairing dependency: core.unitForPage() is a plain function call, so the `unit` binding
    // below re-evaluates only when active/core/currentPage change — NOT when the backend's pairing
    // becomes available. On a FRESH open in double mode (manga's default now), the surface is active
    // before the volume finishes loading, so the FIRST unit is empty and, without this, never
    // recomputes when the entry arrives -> the page stays BLACK. entryRev is bumped on the core's
    // entryChanged + pairingChanged (ComicReaderCore.cpp:201-202, and every rebuildUnits) and folded
    // into the unit binding; the same signal re-runs _onUnitShown() so setVisible() drives the decode.
    property int entryRev: 0
    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageReady(page)  { root.readyRev += 1 }
        function onEntryChanged()   { root.entryRev += 1; root._onUnitShown() }
        function onPairingChanged() { root.entryRev += 1; root._onUnitShown() }
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
    readonly property var unit: (root.entryRev, (active && core && core.unitForPage)
        ? _normUnit(core.unitForPage(Math.max(0, currentPage - 1)))
        : _emptyUnit)

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

    // Decode cap, ported from the reader this replaced (its `pagedSrcW`): cap the decoded width so a
    // page costs what it is DISPLAYED at, not its full scan resolution — but raise the cap as you
    // zoom in, or magnification would just be showing you a bigger blur.
    readonly property int srcCapW: clampedZoom >= 180 ? 2800 : (clampedZoom > 100 ? 2048 : 1400)

    // ================= ONE scale for the whole displayed unit =================
    // Scanned volumes are trimmed page by page — 1550x2200 next to 1500x2200 is routine. Sizing each
    // half to the SAME half-width draws one page visibly larger than the other: tops aligned, bottoms
    // ragged, art scale jumping across the gutter, the spread reading as a collage rather than as one
    // piece of paper. Both lineage readers compute ONE scale for the displayed unit and apply it to
    // both halves (Reader 1 ReaderEngine.js computeSpreadLayout — `Math.min` over the two halves'
    // fits, called with fitWidth:true; Tankoban 2 ComicReader.cpp "B2: Unified pair scale — both
    // pages at identical heights"). Fit is by WIDTH, exactly as both of them do it: the unit fills
    // the viewport width and vertical overflow is what pan is for.
    //
    // NATURAL SIZE = the page's TRUE, UNCAPPED source geometry, from the BACKEND — pageInfo()'s
    // sourceWidth/sourceHeight (PageMeta::sourceSize, learned from the file HEADER ahead of the
    // decode). NOT the Image's implicitWidth: `sourceSize.width: srcCapW` makes
    // ComicReaderProvider's response hand back every page already scaled to that same capped
    // width, so two differently-trimmed scans BOTH report implicitWidth == 1400 and a shared scale
    // computed off that is arithmetically identical to sizing each half on its own — a fix that
    // fixes nothing on exactly the books it is for. (Same reason the strip sizes its column from the
    // model's displayWidth/displayHeight rather than from the loaded Image.) The Image's implicit
    // size stays as the FALLBACK, for a core that reports no geometry.
    function _naturalSize(index, img) {
        var w = 0, h = 0
        if (index >= 0 && core && core.pageInfo) {
            var info = core.pageInfo(index)
            if (info) {
                w = info.sourceWidth  > 0 ? info.sourceWidth  : 0
                h = info.sourceHeight > 0 ? info.sourceHeight : 0
            }
        }
        if (!(w > 0 && h > 0)) { w = img.implicitWidth; h = img.implicitHeight }
        return { w: (w > 0 ? w : 0), h: (h > 0 ? h : 0) }
    }
    // readyRev/entryRev are the same refresh dependencies the `source`/`unit` bindings carry:
    // pageInfo() is a plain call, so without them the geometry would never pick up a size the
    // backend learned after this binding first ran.
    readonly property var _rightNat: (root.readyRev, root.entryRev,
                                      _naturalSize(root.unit.rightIndex, rightImg))
    readonly property var _leftNat:  (root.readyRev, root.entryRev,
                                      _naturalSize(root.unit.leftIndex, leftImg))

    // THE one scale. A half whose size is not known yet contributes nothing — when only ONE half has
    // decoded we fit that one alone rather than collapsing the whole unit to zero.
    readonly property real unitScale: {
        if (isPair) {
            var sr = _rightNat.w > 0 ? _halfW / _rightNat.w : 0
            var sl = _leftNat.w  > 0 ? _halfW / _leftNat.w  : 0
            if (sr > 0 && sl > 0) return Math.min(sr, sl)
            return sr > 0 ? sr : sl
        }
        return _rightNat.w > 0 ? _contentW / _rightNat.w : 0
    }

    // Drawn geometry. With no natural size at all (nothing decoded, no backend geometry) each half
    // falls back to its full box — the same placeholder the surface has always drawn.
    readonly property real _rightW: _rightNat.w > 0 ? _rightNat.w * unitScale : (isPair ? _halfW : _contentW)
    readonly property real _rightH: _rightNat.h > 0 ? _rightNat.h * unitScale : height
    readonly property real _leftW:  _leftNat.w  > 0 ? _leftNat.w  * unitScale : _halfW
    readonly property real _leftH:  _leftNat.h  > 0 ? _leftNat.h  * unitScale : height

    // The unit as ONE block: its height, and its top in viewport coords. It CENTRES when it fits —
    // pinned to the top with all the black below reads as a layout bug — and pan owns the offset
    // once it is taller than the viewport (where the centring term is 0 anyway).
    readonly property real unitHeight: isPair ? Math.max(_rightH, _leftH) : (isSingle ? _rightH : 0)
    readonly property real unitTop: Math.max(0, (height - unitHeight) / 2) - panY
    readonly property real panYMax: Math.max(0, unitHeight - height)

    // Flush at the spine: both inner edges land on the centre line. The PHYSICAL side flips with
    // direction — RTL (manga) puts the rightIndex page on the physical RIGHT, LTR mirrors it.
    readonly property real _spineX: _halfW
    readonly property real _rightX: isPair ? (rtl ? _spineX : _spineX - _rightW) : 0
    readonly property real _leftX:  rtl ? _spineX - _leftW : _spineX
    // ...and each half centres inside the pair's band, so a shorter page sits mid-height.
    readonly property real _rightY: unitTop + (unitHeight - _rightH) / 2
    readonly property real _leftY:  unitTop + (unitHeight - _leftH) / 2

    function setZoom(pct) {
        zoomPercent = Math.max(100, Math.min(260, Math.round(pct)))
        // Keep the reader's place: clamp the existing pan into the new bounds rather than zeroing it.
        // A zoom step used to teleport you to the corner of the spread — in RTL manga, the far end of
        // the page you were reading. Both lineage readers clamp and never zero (Tankoban 2
        // ComicReader.cpp applyPan; Reader 1 MangaReader.qml zoomBy -> clampPan()). Pan is reset by a
        // UNIT change, in _onUnitShown() — a new spread, not a zoom step.
        panX = Math.max(0, Math.min(panXMax, panX))
        panY = Math.max(0, Math.min(panYMax, panY))
    }
    function zoomIn()  { setZoom(clampedZoom + 20) }
    function zoomOut() { setZoom(clampedZoom - 20) }
    function panBy(dx, dy) {
        panX = Math.max(0, Math.min(panXMax, panX + dx))
        panY = Math.max(0, Math.min(panYMax, panY + dy))
    }
    function next()     { manualNavigation(); nextRequested() }
    function previous() { manualNavigation(); previousRequested() }

    // ================= drawn geometry (viewport coords) — for direction assert + HUD =================
    // These READ THE ITEMS rather than re-deriving the math, so they cannot drift from what is
    // actually on screen. rightIndex page: physical RIGHT in RTL, physical LEFT in LTR; leftIndex
    // page mirrors it — unchanged meaning, the shell's right-click page mapping depends on it.
    readonly property real rightIndexX: content.x + rightImg.x
    readonly property real leftIndexX:  content.x + leftImg.x
    readonly property real rightIndexY: content.y + rightImg.y
    readonly property real leftIndexY:  content.y + leftImg.y
    readonly property real rightPageWidth:  rightImg.width
    readonly property real rightPageHeight: rightImg.height
    readonly property real leftPageWidth:   leftImg.width
    readonly property real leftPageHeight:  leftImg.height
    readonly property real rightNaturalWidth:  _rightNat.w
    readonly property real rightNaturalHeight: _rightNat.h
    readonly property real leftNaturalWidth:   _leftNat.w
    readonly property real leftNaturalHeight:  _leftNat.h
    readonly property real singleImageWidth: rightImg.width
    readonly property alias gutterShadowItem: gutterShadow
    readonly property bool gutterVisible: gutterShadow.visible
    readonly property alias rightSource: rightImg.source   // exposed so the decode-refresh test sees the source re-evaluate
    readonly property alias leftSource: leftImg.source

    // ============ unit lifecycle: reset PAN (zoom persists), pin pages, drive maxSeen ============
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
        // A new unit resets the PAN to origin — never the zoom. Both lineage readers keep
        // zoom across turns (TB2 even persists it per series); resetting it made a magnified
        // volume unreadable: zoom in, turn the page, back to 100%, every single turn.
        panX = 0; panY = 0
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
            cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on a real redecode, and
                           // WITHOUT the pixmap cache every delegate rebuild re-pays the provider's full-res
                           // SmoothTransformation downscale — the "scroll back up and it stutters" cost.
            retainWhileLoading: true
            // Still PreserveAspectFit even though width/height are now exact-ratio: it is the safety
            // net for the one case where they are not — the backend's header geometry disagreeing
            // with the decoded pixels. Letterboxed inside its box beats stretched.
            fillMode: Image.PreserveAspectFit
            sourceSize.width: root.srcCapW
            mipmap: true
            width: root._rightW
            height: root._rightH
            x: root._rightX
            y: root._rightY
        }

        // leftIndex page (only for a real pair)
        Image {
            id: leftImg
            visible: root.isPair
            source: (root.readyRev, (visible && root.core && root.core.imageUrl && root.unit.leftIndex >= 0)
                    ? root.core.imageUrl(root.unit.leftIndex) : "")
            asynchronous: true
            cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on a real redecode, and
                           // WITHOUT the pixmap cache every delegate rebuild re-pays the provider's full-res
                           // SmoothTransformation downscale — the "scroll back up and it stutters" cost.
            retainWhileLoading: true
            fillMode: Image.PreserveAspectFit
            sourceSize.width: root.srcCapW
            mipmap: true
            width: root._leftW
            height: root._leftH
            x: root._leftX
            y: root._leftY
        }

        // gutter shadow — soft spine, only for a real pair
        Rectangle {
            id: gutterShadow
            // Follows the PAGES, not the viewport: a shadow the full window height darkens empty
            // black above and below the spread instead of shading the spine.
            visible: root.isPair && root.gutterStrength > 0
            width: 18
            height: root.unitHeight
            x: root._spineX - width / 2
            y: root.unitTop
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
