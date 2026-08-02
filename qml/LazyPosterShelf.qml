// LazyPosterShelf — a stable-height host that mounts one PosterRail ONLY around the WorldPage
// viewport (Catalogue Poster & Shelf Polish, Task 5). A long catalogue has many row descriptors,
// but live rails/delegates/images are bounded to the viewport window. The host:
//   • reserves the rail's EXACT full height at all times, so mounting/unmounting a shelf never
//     moves vertical scroll position;
//   • enters (mounts) when its span intersects the viewport expanded by ONE viewport, and leaves
//     (unmounts) only when its span no longer intersects the viewport expanded by TWO viewports —
//     the hysteresis prevents create/destroy thrashing at the threshold;
//   • stores the rail's latest horizontal contentX before unload and hands it back on remount;
//   • falls back to mounted when no viewport is known (offscreen harnesses), so pages never blank.
// It owns NO vertical scroller of its own — WorldPage remains the only vertical scroll owner.
import QtQuick
import "CatalogueVisualMetrics.js" as Metrics

Item {
    id: shelf

    // ── inputs ──
    property var row: null                    // { title, ranked, items, sourceKind, sourceLabel, seeAllPin, hidden }
    property string visualProfile: "classic"
    property real viewportTop: 0              // viewport top in TheatreCatalogPage-local coordinates
    property real viewportHeight: 0           // 0 → viewport unknown; mount as an offscreen fallback
    property bool editMode: false
    property real activationViewports: 1.0
    property real retentionViewports: 2.0

    // ── outputs ──
    signal itemRequested(var item)
    signal seeAllRequested(var pin)

    readonly property var _m: Metrics.profile(shelf.visualProfile)
    readonly property bool _gallery: shelf.visualProfile === "gallery"
    readonly property bool _hasItems: shelf.row && shelf.row.items && shelf.row.items.length > 0
    // rail geometry, computed from the frozen tokens so the reserve matches the real PosterRail
    // exactly (WidgetHeader 30 + Column headerGap + poster height + title reserve) — no jump on mount.
    readonly property int _posterH: Math.floor(shelf._m.posterWidth * shelf._m.posterRatio)
    readonly property int _titleReserve: (shelf._gallery ? 10 : 8) + shelf._m.titleMinHeight
    readonly property int _railListHeight: shelf._posterH + shelf._titleReserve
    readonly property real reservedHeight: shelf._hasItems
        ? (30 + shelf._m.headerGap + shelf._railListHeight) : 0

    property bool railLoaded: false
    // the horizontal position preserved across unload; handed to the remounted rail as initialContentX.
    property real savedContentX: 0
    readonly property real restoredContentX: shelf.savedContentX

    width: parent ? parent.width : 900
    height: reservedHeight
    // hidden rows dim in edit mode, exactly as the eager PosterRail delegate did.
    opacity: (shelf.editMode && shelf.row && shelf.row.hidden === true) ? 0.5 : 1

    // ── residency: pure geometry, re-evaluated on every input that can change the answer ──
    function _intersects(top, bottom, bandTop, bandBottom) {
        return bottom >= bandTop && top <= bandBottom;
    }
    function evaluateResidency() {
        if (shelf.viewportHeight <= 0) {          // viewport unknown → mount (offscreen fallback)
            shelf.railLoaded = true;
            return;
        }
        var vh = shelf.viewportHeight;
        var vTop = shelf.viewportTop;
        var vBot = vTop + vh;
        var top = shelf.y;
        var bottom = shelf.y + shelf.reservedHeight;
        if (!shelf.railLoaded) {
            // ENTER when intersecting the one-viewport activation band
            if (_intersects(top, bottom, vTop - shelf.activationViewports * vh,
                            vBot + shelf.activationViewports * vh))
                shelf.railLoaded = true;
        } else {
            // LEAVE only when no longer intersecting the two-viewport retention band
            if (!_intersects(top, bottom, vTop - shelf.retentionViewports * vh,
                             vBot + shelf.retentionViewports * vh)) {
                // store the rail's latest position before it is torn down
                if (railLoader.item)
                    shelf.savedContentX = railLoader.item.currentContentX;
                shelf.railLoaded = false;
            }
        }
    }

    onViewportTopChanged: shelf.evaluateResidency()
    onViewportHeightChanged: shelf.evaluateResidency()
    onYChanged: shelf.evaluateResidency()
    onReservedHeightChanged: shelf.evaluateResidency()
    onRowChanged: shelf.evaluateResidency()
    onEditModeChanged: shelf.evaluateResidency()   // must NOT force far shelves live (residency ignores it)
    Component.onCompleted: shelf.evaluateResidency()

    // test seam — record a scrolled horizontal position (persisted; also pushed to a live rail).
    function testSetRailContentX(x) {
        shelf.savedContentX = x;
        if (railLoader.item)
            railLoader.item.testSetContentX(x);
    }

    Loader {
        id: railLoader
        width: shelf.width
        active: shelf.railLoaded
        sourceComponent: railComponent
    }

    Component {
        id: railComponent
        PosterRail {
            width: shelf.width
            title: shelf.row ? (shelf.row.title || "") : ""
            ranked: shelf.row ? shelf.row.ranked === true : false
            items: (shelf.row && shelf.row.items !== undefined) ? shelf.row.items : []
            sourceKind: shelf.row && shelf.row.sourceKind !== undefined ? shelf.row.sourceKind : "house"
            sourceLabel: shelf.row && shelf.row.sourceLabel !== undefined ? shelf.row.sourceLabel : ""
            seeAllPin: shelf.row && shelf.row.seeAllPin !== undefined ? shelf.row.seeAllPin : null
            visualProfile: shelf.visualProfile
            initialContentX: shelf.savedContentX
            onItemRequested: (item) => shelf.itemRequested(item)
            onSeeAllRequested: (pin) => shelf.seeAllRequested(pin)
            // Position is captured from currentContentX at unload (evaluateResidency), NOT tracked
            // continuously — that avoids the rail's transient pre-restore contentX (-6) clobbering the
            // saved value during mount. The rail's own restore-from-initialContentX is proven in Task 4.
        }
    }
}
