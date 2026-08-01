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
// THE UNIT PAINTS AS ONE THING (Task 4, overhaul plan 2026-07-28). Until now this surface bound each
// half's Image straight to the stage, so whichever half decoded first appeared and the other stayed
// black — a spread arriving as one page plus a hole. Hemanth's approved wording is the rule: "A
// paired spread appears as one complete unit. We never flash the left page first and leave the right
// half black."
//
// THE PAINT RULE IS WHAT THE SCREEN HAS, not what the backend has. A half is RESOLVED when its pixels
// are up, or when it has failed terminally and its typed placard is the honest answer for it; the
// unit paints when every half it has is resolved, and not one beat sooner. While it waits, one
// restrained placeholder per half stands exactly where that page will land.
//
// It is deliberately NOT gated on core.presentationForPage. The first version of this task was, and
// that left the defect open in two shapes — see the unitPaints block below for the measurements. The
// backend verdict is strictly EARLIER than the pixels (each half comes back through its own async
// provider round trip, and the very pageReady that flips the verdict to "ready" is what re-points the
// second half's Image at its bumped url), so it can report a unit ready in the beat the second half
// starts loading. presentationForPage stays as the backend's unit-level verdict + reason — exposed
// here as presentationState, and what Task 11's retry needs in order to say WHY a unit is not
// showing — but it decides no painting.
//
// A core WITHOUT presentationForPage (a fake, never production) therefore changes nothing about the
// paint: the gate reads its own Images either way, and such a core paints exactly when its pages are
// on screen.
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
    // The unit is ACTUALLY on screen now — not "was asked for", which is what unitShown reports.
    // Fired once per unit, the moment every half it has is resolved (pixels up, or a terminal failure
    // showing its typed placard). Same predicate as the paint gate, by construction.
    // `withinPageFraction` is always 0 here (a unit is the viewport's whole travel in this layout);
    // it rides in the signature so all three surfaces speak one shape. Task 11 is the consumer that
    // gates progress-saving on it; until then it is emitted and unused, which is expected.
    signal presented(int anchorPage, real withinPageFraction)
    // The placard's two ways out (Task 11), raised straight through. A pair can show TWO of these
    // cards at once, so the page number is the card's own — never `currentPage`, which would retry
    // the good half. The surface performs neither action: Retry is a backend re-read and Skip is a
    // navigation, and both belong to the shell. 1-based, like every page number this surface reports.
    signal retryRequested(int page)
    signal skipRequested(int page)

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
    // Failure dependency, added with the presentation gate: presentationForPage()'s answer flips to
    // "error" on a pageFailed, and that signal changes nothing else QML can see. Without this the
    // unit would sit on its placeholder forever waiting for a page that already gave up.
    property int failedRev: 0
    Connections {
        target: root.core
        ignoreUnknownSignals: true
        function onPageReady(page)  { root.readyRev += 1 }
        // The Image panel adjusted the picture (Task 7) — the same refresh dependency as a
        // decode landing: imageUrl() folds the render revision C++-side, invisibly to QML, so
        // without this bump the `source` binding never re-evaluates and QML's own pixmap
        // cache keeps serving the pre-adjustment page.
        function onRenderProfileChanged() { root.readyRev += 1 }
        function onPageFailed(page, code) { root.failedRev += 1 }
        // A retry cleared this page's verdict. pageInfo()/presentationForPage() change their answer
        // and nothing else QML can see moves with them, so without this bump the placard sits there
        // through the whole re-read. Same dependency as a failure — the same question, new answer.
        function onPageRetried(page) { root.failedRev += 1 }
        function onEntryChanged()   {
            root.entryRev += 1
            // A NEW BOOK has to be able to present the same anchor page as the last one. Without this
            // reset, opening book B on page 1 straight after book A page 1 emitted nothing at all,
            // because the marker still said "1 is presented". Measured: 0 emissions — and unlike the
            // warm-pixmap case below, this one does not depend on any cache behaviour.
            root._presentedAnchor = -1
            root._onUnitShown()
            root._checkPresented()
        }
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

    // ================= the unit's PRESENTATION (Task 4) =================
    //
    // TWO different questions, and conflating them was the bug this block now separates:
    //   presentationState  — what the BACKEND has (every member decoded, or one terminally failed).
    //   unitPaints         — what the SCREEN has. This is the paint rule.
    //
    // readyRev / failedRev / entryRev are folded into the backend read for the usual reason:
    // presentationForPage() is a plain call whose answer changes on signals QML cannot otherwise see.

    // ---- the backend's verdict (presentationForPage) ----
    readonly property var _readyPresentation: ({ state: "ready", errorCode: "none" })
    function _normPresentation(p) {
        // An EMPTY map is the core's "no entry / no pairing yet" answer (the same one unitForPage
        // gives) — genuinely nothing to show, so it waits.
        if (!p || p.state === undefined)
            return { state: "waiting", errorCode: "none" }
        return { state: String(p.state),
                 errorCode: (p.errorCode !== undefined ? String(p.errorCode) : "none") }
    }
    readonly property var presentation: (root.readyRev, root.failedRev, root.entryRev,
        (active && core && core.presentationForPage)
            ? _normPresentation(core.presentationForPage(Math.max(0, currentPage - 1)))
            // A core without the query is a fake, never production. It reads "ready" so the exposed
            // state is not a misleading "waiting"; it no longer decides anything, because the paint
            // gate below does not consult the verdict at all.
            : _readyPresentation)
    readonly property string presentationState: presentation.state

    // ---- WHAT THE SCREEN HAS. This is the paint rule. ----
    // A half is RESOLVED when there is something real to show for it: its pixels arrived, or it failed
    // terminally and its typed placard is the honest answer. The unit paints when EVERY half it has is
    // resolved, and not one beat sooner.
    //
    // Gating on the backend verdict instead was wrong in two ways that look different and are the same
    // bug — the verdict is strictly EARLIER than the pixels:
    //
    //   * Each half's pixels come back through a SEPARATE async provider round trip, and the very
    //     pageReady that flips the verdict to "ready" is also what re-points the second half's Image at
    //     its bumped ?rev= url. So the gate opened in the beat the second half STARTED loading, over a
    //     solid black stage: one page beside a black rectangle, the exact thing this task exists to
    //     prevent. (Measured: same beat as pageReady, pairVisible=true with the second half's pixels
    //     30-40ms away — and that was with 200x300 fixtures; real scans go through the provider's full
    //     SmoothTransformation downscale and take longer.)
    //   * "error" also opens the verdict, so the instant EITHER member failed both placeholders
    //     vanished while the SURVIVING half might still be decoding — an error placard beside a black
    //     rectangle, and worse than the frame before it, because the restrained panel standing in for
    //     that page disappeared. A corrupt page fails on a header read and a healthy one takes an order
    //     of magnitude longer, and one setVisible dispatches both, so that ordering is the LIKELY one.
    //     (Measured: state=error, pairVisible=true, placeholdersShown=false, good half with no pixels.)
    //
    // One rule fixes both, and it makes the paint rule and presented() the SAME predicate instead of two
    // that disagree about when a unit is on screen.
    readonly property bool rightHalfResolved: root.rightErrorCode.length > 0
                                              || rightImg.status === Image.Ready
    readonly property bool leftHalfResolved: root.leftErrorCode.length > 0
                                             || leftImg.status === Image.Ready
    readonly property bool unitResolved: (root.isPair || root.isSingle)
                                         && rightHalfResolved
                                         && (!root.isPair || leftHalfResolved)

    // ONE term, not two. An earlier draft also ANDed the backend verdict in, reasoning that on a turn
    // onto a not-yet-decoded unit the verdict goes "waiting" in the same pass that moves the geometry,
    // whereas an Image still holding the OUTGOING unit's pixels (retainWhileLoading) might read Ready
    // for a beat longer and paint the old pages at the new unit's size. Measured: it does not — the
    // source binding re-evaluates in that same pass and Qt drops the status out of Ready synchronously,
    // so rightHalfResolved was already false in the beat of the turn. The extra term bought nothing, so
    // it is gone.
    //
    // Which leaves presentationForPage OUT of the paint path, and that is worth saying plainly rather
    // than hiding: pixels-up implies decoded (the provider can only serve what the backend decoded), so
    // the verdict is a strictly EARLIER and weaker signal than the screen's and cannot be the paint
    // rule. It stays as the backend's unit-level verdict + reason — exposed as presentationState,
    // tested by the core harness (T29-T31), and what Task 11's retry needs in order to say WHY a unit
    // is not showing.
    //
    // An ABSENT unit (no entry, no pairing yet) resolves to false through the isPair/isSingle term, so
    // a reader with no book open paints nothing rather than an empty spread.
    readonly property bool unitPaints: unitResolved

    // Per-member failure attribution. The unit-level errorCode cannot name WHICH half broke, and a
    // pair has two, so each half asks the backend about itself. pageInfo() is also self-healing (a
    // MissingFile page that comes back reports "none" again), which a locally cached failure map
    // would not be.
    function _pageErrorCode(index) {
        if (index < 0 || !core || !core.pageInfo) return ""
        var info = core.pageInfo(index)
        if (!info || info.error === undefined) return ""
        var e = String(info.error)
        return (e === "" || e === "none") ? "" : e
    }
    readonly property string rightErrorCode: (root.readyRev, root.failedRev, root.entryRev,
                                              _pageErrorCode(root.unit.rightIndex))
    readonly property string leftErrorCode:  (root.readyRev, root.failedRev, root.entryRev,
                                              _pageErrorCode(root.unit.leftIndex))

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
    // The gutter's OWN rule (pair, and gutterStrength > 0), not its effective visibility — see the
    // two-level note on the readbacks at the bottom of this file. A spread with the gate open and a
    // pair with the gate shut must give different answers here, and `.visible` cannot.
    readonly property bool gutterVisible: gutterShadow.ownVisible
    readonly property alias rightSource: rightImg.source   // exposed so the decode-refresh test sees the source re-evaluate
    readonly property alias leftSource: leftImg.source

    // ---- WHAT THIS SURFACE IS DRAWING, and where (Task 9, the Loupe's one question) ----
    // [{ page (0-based), url, x, y, width, height }] in THIS surface's coordinates — the shape all
    // three surfaces answer in, so the lens has one code path for Single, Pair and Long Strip.
    //
    // A pair answers with TWO entries, and that is exactly what lets the lens straddle the gutter:
    // it magnifies every box it is given about the anchor and shows whichever ones reach the glass,
    // so a lens over the spine shows the inner edge of both pages with no case of its own. The
    // boxes are the DRAWN readbacks above (pan and zoom already folded in), never re-derived.
    //
    // The url carries no decode cap: the caller states its own request size, and the Loupe
    // deliberately asks for far more than the screen shows.
    function visiblePageRects() {
        if (!active) return []
        var out = []
        var u = root.unit
        if ((root.isPair || root.isSingle) && u.rightIndex >= 0 && rightImg.width > 0 && rightImg.height > 0)
            out.push({ page: u.rightIndex,
                       url: (core && core.imageUrl) ? core.imageUrl(u.rightIndex, "hq") : "",
                       x: rightIndexX, y: rightIndexY,
                       width: rightPageWidth, height: rightPageHeight })
        if (root.isPair && u.leftIndex >= 0 && leftImg.width > 0 && leftImg.height > 0)
            out.push({ page: u.leftIndex,
                       url: (core && core.imageUrl) ? core.imageUrl(u.leftIndex, "hq") : "",
                       x: leftIndexX, y: leftIndexY,
                       width: leftPageWidth, height: leftPageHeight })
        return out
    }

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
    onCurrentPageChanged: { _onUnitShown(); _checkPresented() }   // a page turn -> a new unit
    // Becoming the mounted surface with the unit already decoded IS a presentation — the layout
    // switched and the reader is now looking at it — so the notice is re-checked here too.
    onActiveChanged: { _onUnitShown(); _checkPresented() }

    // ================= the spread =================
    Item {
        id: content
        width: root._contentW
        height: root.height
        x: -root.panX                                  // horizontal pan slides the zoomed content

        // ---- WAITING: one restrained panel per half, laid out exactly where that page will land, so
        // the unit settles into place rather than jumping into it. Declared BEFORE the gate so it sits
        // UNDERNEATH the pages: a panel fading out over a page that has just arrived would dim it for
        // 140ms, which is the artefact this ordering avoids.
        //
        // Shown for the whole time the unit is not paintable — NOT per half. The gate is
        // all-or-nothing, so a half whose pixels landed early is hidden anyway and must keep its
        // stand-in; a per-half condition would have taken one panel away and shown black in its place.
        // It fades in over 140ms, so a unit that decodes fast never flashes it - see
        // ComicReaderUnitPlaceholder. ----
        ComicReaderUnitPlaceholder {
            id: rightPlaceholder
            objectName: "rightUnitPlaceholder"
            shown: (root.isPair || root.isSingle) && !root.unitPaints
            width: root._rightW
            height: root._rightH
            x: root._rightX
            y: root._rightY
        }
        ComicReaderUnitPlaceholder {
            id: leftPlaceholder
            objectName: "leftUnitPlaceholder"
            shown: root.isPair && !root.unitPaints
            width: root._leftW
            height: root._leftH
            x: root._leftX
            y: root._leftY
        }

        // ---- THE GATE (Task 4). Everything the unit is MADE OF lives under this one Item, so the
        // unit appears in one step or not at all - never one decoded half beside a black rectangle.
        // It is a visibility gate ONLY: the Images below keep their own `visible` (the pair-vs-spread
        // rule), keep loading while it is closed, and keep their geometry live, so every
        // drawn-geometry readback and every source binding behaves exactly as it did before. ----
        Item {
            id: pairPages
            anchors.fill: parent
            visible: root.unitPaints

            // rightIndex page (also the single/spread image, full width)
            Image {
                id: rightImg
                // A half whose own page is terminally broken hands its box to the placard instead.
                //
                // The rule is a NAMED property that `visible` is then bound to, and every other item
                // under this gate does the same. QQuickItem.visible READS BACK EFFECTIVE visibility —
                // a child of a hidden parent reports false whatever its own binding says — so once
                // this Item became a gate, a readback of `.visible` stopped being able to express
                // "this half's own rule" at all and started reporting the gate. Naming the rule keeps
                // the readbacks at the bottom honest without re-deriving anything: they read the very
                // property the binding uses, so they cannot drift from what is drawn.
                property bool ownVisible: (root.isPair || root.isSingle) && root.rightErrorCode.length === 0
                visible: ownVisible
                // NOTE: the source guard is the UNIT SHAPE, not this Image's `visible`, which now
                // also carries the error state - a failed page must keep asking for its url so a
                // heal (MissingFile is a cooldown, not a life sentence) can still land.
                source: (root.readyRev, ((root.isPair || root.isSingle) && root.core
                                         && root.core.imageUrl && root.unit.rightIndex >= 0)
                        ? root.core.imageUrl(root.unit.rightIndex) : "")
                asynchronous: true
                cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on a real redecode, and
                               // WITHOUT the pixmap cache every delegate rebuild re-pays the provider's full-res
                               // SmoothTransformation downscale - the "scroll back up and it stutters" cost.
                retainWhileLoading: true
                // Still PreserveAspectFit even though width/height are now exact-ratio: it is the safety
                // net for the one case where they are not - the backend's header geometry disagreeing
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
                property bool ownVisible: root.isPair && root.leftErrorCode.length === 0
                visible: ownVisible
                source: (root.readyRev, (root.isPair && root.core && root.core.imageUrl
                                         && root.unit.leftIndex >= 0)
                        ? root.core.imageUrl(root.unit.leftIndex) : "")
                asynchronous: true
                cache: true    // SAFE and load-bearing: the ?rev= in the url self-busts on a real redecode, and
                               // WITHOUT the pixmap cache every delegate rebuild re-pays the provider's full-res
                               // SmoothTransformation downscale - the "scroll back up and it stutters" cost.
                retainWhileLoading: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: root.srcCapW
                mipmap: true
                width: root._leftW
                height: root._leftH
                x: root._leftX
                y: root._leftY
            }

            // gutter shadow - soft spine, only for a real pair
            Rectangle {
                id: gutterShadow
                // Follows the PAGES, not the viewport: a shadow the full window height darkens empty
                // black above and below the spread instead of shading the spine.
                property bool ownVisible: root.isPair && root.gutterStrength > 0
                visible: ownVisible
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

            // ---- typed placards, per HALF. The unit-level errorCode cannot say WHICH page broke and
            // a pair has two, so each side reads its own. They sit INSIDE the gate, beside a good half
            // that paints normally: "the good side plus an explicit error side", never a whole unit
            // replaced by one card. ----
            ComicReaderUnitError {
                id: rightErrorPlacard
                objectName: "rightUnitError"
                property bool ownVisible: root.rightErrorCode.length > 0 && (root.isPair || root.isSingle)
                visible: ownVisible
                code: root.rightErrorCode
                pageIndex: root.unit.rightIndex
                width: root._rightW
                height: root._rightH
                x: root._rightX
                y: root._rightY
                // THIS half's page, not the anchor: the whole point of per-half placards is that the
                // good side keeps reading, so the actions must name the side that actually broke.
                onRetryRequested: function (page) { root.retryRequested(page + 1) }
                onSkipRequested: function (page) { root.skipRequested(page + 1) }
            }
            ComicReaderUnitError {
                id: leftErrorPlacard
                objectName: "leftUnitError"
                property bool ownVisible: root.leftErrorCode.length > 0 && root.isPair
                visible: ownVisible
                code: root.leftErrorCode
                pageIndex: root.unit.leftIndex
                width: root._leftW
                height: root._leftH
                x: root._leftX
                y: root._leftY
                onRetryRequested: function (page) { root.retryRequested(page + 1) }
                onSkipRequested: function (page) { root.skipRequested(page + 1) }
            }
        }

    }

    // ---- presented(): the unit is ACTUALLY on screen. THE SAME PREDICATE THE GATE USES, so the two
    // cannot drift apart again — which they had, the gate trusting the backend while presented()
    // correctly waited for pixels.
    //
    // A terminally failed half COUNTS as presented, and that is the one rule all three surfaces now
    // follow: presented() means "the reader can see this position's content, or an explicit account of
    // why not". Task 11 must not sit waiting to bank a position that can never render — the reader is
    // there either way, and whether to offer a retry is a separate question from where they are.
    //
    // DERIVED, not hung off each Image's onStatusChanged: right-then-left arriving is ONE state change
    // this way, so it is one presentation rather than two.
    //
    // But a derived property alone is NOT enough to NOTICE one, and that hole is why the unit-change
    // path re-checks explicitly:
    //
    //   * A NEW BOOK opening on the same page number the last one was left on. The marker still says
    //     that page is presented, so nothing fires, and no cache behaviour is involved — this one is
    //     unconditional. Handled by resetting the marker on entryChanged, above.
    //   * A turn onto a unit whose pixmaps are still in QQuickPixmapCache: both Images go straight to
    //     Ready with no dip, the property never CHANGES, and nothing is emitted. MEASURED 2026-07-30,
    //     because two reviewers got opposite answers on it. Both were right about their own run: at
    //     140/280px request sizes the turn back onto a just-left page keeps status Ready and the
    //     derived property fires ZERO times; at this surface's real cap (srcCapW 1400 -> a 1400x2100
    //     pixmap, ~11 MB, against QQuickPixmapStore's 10 MB desktop limit for UNREFERENCED pixmaps)
    //     the page is evicted the moment you turn away, so the turn back genuinely re-loads and the
    //     status does dip. So today the app is saved by a pixmap being too big to cache — an
    //     undocumented limit that moves with the cap, the screen and the Qt version. A signal Task 11
    //     banks progress on does not get to rest on that.
    //
    // The re-check is deferred through Qt.callLater so the source/status bindings have settled before
    // it reads them. Firing exactly once is the MARKER's job, not callLater's — but callLater does
    // collapse repeats within one pass (measured: three schedules of the same method, one invocation),
    // so a turn that also dips a status does not even queue redundant work. ----
    property int _presentedAnchor: -1
    // Literally the paint rule, aliased for readability — if the reader can see it, it is presented,
    // and the two cannot drift apart again.
    readonly property bool contentOnScreen: unitPaints
    function _notePresented() {
        if (!active || !contentOnScreen) return
        if (_presentedAnchor === root.currentPage) return
        _presentedAnchor = root.currentPage
        root.presented(root.currentPage, 0)
    }
    function _checkPresented() { Qt.callLater(root._notePresented) }
    onContentOnScreenChanged: _notePresented()

    // ---- readbacks the gate added, for the harness. ----
    //
    // TWO LEVELS, and keeping them apart is the whole point. `pairVisible` is THE GATE — pairPages is
    // a direct child of the always-visible `content`, so its `visible` is its own binding and reading
    // it reads the paint rule. Everything else under the gate exposes that item's OWN rule
    // (`ownVisible`), NOT its effective visibility, because QQuickItem.visible reads back EFFECTIVE
    // visibility: once the gate closes, every child reports false regardless of its binding, and a
    // readback of `.visible` would silently stop testing the pair-vs-spread / which-half-failed rules
    // and start re-testing the gate. (That is not hypothetical — it turned the gutter-shadow
    // assertions green-then-red the moment the gate started depending on pixels.) What is actually
    // ON SCREEN is `pairVisible && <ownVisible>`, and both terms are asserted.
    //
    // They still READ THE ITEMS rather than re-deriving the conditions: `ownVisible` is the very
    // property each item's `visible` is bound to, so a test cannot pass against a rule the screen does
    // not follow.
    readonly property bool pairVisible: pairPages.visible
    readonly property bool rightImageVisible: rightImg.ownVisible
    readonly property bool leftImageVisible: leftImg.ownVisible
    readonly property bool rightErrorVisible: rightErrorPlacard.ownVisible
    readonly property bool leftErrorVisible: leftErrorPlacard.ownVisible
    // ...and whether each card is offering its way out. Its OWN rule, never its effective
    // `visible` — see the note above, and the one in ComicReaderUnitError.
    readonly property bool rightErrorActionsShown: rightErrorPlacard.actionsShown
    readonly property bool leftErrorActionsShown: leftErrorPlacard.actionsShown
    readonly property bool placeholdersShown: rightPlaceholder.shown || leftPlaceholder.shown
    // ...and each panel on its own, so a test can pin that a half whose pixels landed EARLY keeps its
    // stand-in while the gate is shut. Dropping it there is not a smaller version of the bug — it is
    // the same bug, drawn by the stand-in instead of by the page.
    readonly property bool rightPlaceholderShown: rightPlaceholder.shown
    readonly property bool leftPlaceholderShown: leftPlaceholder.shown
}
