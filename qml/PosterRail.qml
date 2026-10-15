// PosterRail - lightweight horizontal Theatre rail. Uses the shared CataloguePosterCard so its
// tiles render exactly like the rest of the catalogue; the header (unchanged WidgetHeader: 22px
// Fraunces title, 17px Fraunces See-all) shows only the title, optional factual source attribution,
// and See all. No blurb, no rating line under the posters. Top 10 keeps its oversized rank numerals.
//
// A `visualProfile` ("classic" | "gallery") selects geometry + spacing from CatalogueVisualMetrics.
// The rail gains three additive seams for the polish arc WITHOUT touching horizontal scroll physics:
//   • a saved/restored horizontal position (initialContentX in, horizontalPositionChanged out), so
//     LazyPosterShelf can unload and remount a shelf and land within 1px of where it was;
//   • keyboard/remote traversal (Left/Right move currentIndex, Enter activates the current item);
//   • the gallery card's hover source attribution ("IMDb", the rating's origin).

import QtQuick
import "CatalogueVisualMetrics.js" as Metrics

pragma ComponentBehavior: Bound

Column {
    id: rail

    property string title: ""
    property var items: []
    property bool ranked: false
    property int itemLimit: ranked ? 10 : 20
    // factual source attribution — "" for house shelves, the extension name for extension rows.
    property string sourceLabel: ""
    property string sourceKind: "house"
    // the See-all descriptor for this shelf; a null pin means the rail is not navigable.
    property var seeAllPin: null
    // "classic" | "gallery" — gallery is Theatre's approved polish profile.
    property string visualProfile: "classic"

    // ── horizontal-position seam (saved/restored by LazyPosterShelf across unload/remount) ──
    property real initialContentX: 0
    readonly property real currentContentX: list.contentX
    signal horizontalPositionChanged(real x)

    // ── keyboard/remote traversal ──
    property int currentIndex: 0
    readonly property bool railFocused: list.activeFocus

    readonly property var _m: Metrics.profile(rail.visualProfile)
    readonly property bool _gallery: rail.visualProfile === "gallery"
    readonly property int posterWidth: rail._m.posterWidth
    readonly property int posterHeight: Math.floor(rail.posterWidth * rail._m.posterRatio)
    readonly property int cardGap: rail.ranked ? 30 : rail._m.cardGap
    // reserved space below the poster: the title top gap + the (one- or two-line) title measure.
    readonly property int titleReserve: (rail._gallery ? 10 : 8) + rail._m.titleMinHeight
    readonly property int railListHeight: rail.posterHeight + rail.titleReserve
    // ranked cells grow to seat the oversized numeral; the poster itself stays posterWidth.
    readonly property int cellWidth: rail.ranked ? (rail.posterWidth + 52) : rail.posterWidth
    // the ListView's final contentWidth (cells + gaps, margins excluded) — computed from the model so
    // the restore clamps against the TRUE extent, not a partial mid-layout value.
    readonly property real _fullContentWidth: {
        var n = rail.visibleItems.length;
        return n <= 0 ? 0 : n * rail.cellWidth + (n - 1) * rail.cardGap;
    }

    property var visibleItems: {
        var out = [];
        var count = Math.min(items.length, itemLimit);
        for (var i = 0; i < count; i++)
            out.push(items[i]);
        return out;
    }
    signal itemRequested(var item)
    signal seeAllRequested(var pin)

    width: parent ? parent.width : 900
    spacing: rail._m.headerGap
    visible: visibleItems.length > 0

    // test seam — the harness cannot flick a real Flickable offscreen deterministically.
    function testSetContentX(x) { list.contentX = x }
    // keyboard/remote: Left/Right clamp the current index and keep it in view; Enter activates it.
    function navigate(delta) {
        var n = rail.visibleItems.length;
        if (n <= 0) return;
        rail.currentIndex = Math.max(0, Math.min(n - 1, rail.currentIndex + delta));
        list.positionViewAtIndex(rail.currentIndex, ListView.Contain);
    }
    function activateFocused() {
        if (rail.currentIndex >= 0 && rail.currentIndex < rail.visibleItems.length)
            rail.itemRequested(rail.visibleItems[rail.currentIndex]);
    }

    Theme { id: theme }

    WidgetHeader {
        width: parent.width
        title: rail.title
        // source attribution is metadata, never promotional copy — shown only for extensions.
        sub: (rail.sourceKind !== "house" && rail.sourceLabel.length > 0) ? ("via " + rail.sourceLabel) : ""
        moreLabel: "See all"
        navigable: rail.seeAllPin !== null
        onMoreClicked: rail.seeAllRequested(rail.seeAllPin)
    }

    ListView {
        id: list
        width: parent.width
        height: rail.railListHeight
        orientation: ListView.Horizontal
        spacing: rail.cardGap
        clip: true
        reuseItems: true
        cacheBuffer: width * 0.75
        boundsBehavior: Flickable.StopAtBounds
        model: rail.visibleItems
        leftMargin: 6
        rightMargin: 26
        activeFocusOnTab: true

        // Restore the saved horizontal position ONCE, after layout has a real contentWidth. We wait
        // for contentWidth so the clamp uses the true valid range; then we never re-clamp (a user
        // scroll must win). Emitting on every contentX change writes the host's saved value forward
        // — a plain property write, not a binding, so there is no loop.
        property bool _restored: false
        function _applyInitial() {
            // wait for layout to reach the TRUE content extent — a partial mid-layout contentWidth
            // would clamp the restore to 0. Once applied we never re-clamp, so a user scroll wins.
            if (list._restored || rail._fullContentWidth <= 0) return;
            if (list.contentWidth < rail._fullContentWidth - 1) return;
            var maxX = Math.max(0, list.contentWidth - list.width);
            list.contentX = Math.max(0, Math.min(rail.initialContentX, maxX));
            list._restored = true;
        }
        // Defer past the current layout pass: a fresh ListView positions itself at the start AFTER
        // contentWidth settles, so applying inline would be overwritten. Qt.callLater runs the
        // restore after that pass (and de-duplicates repeated triggers into one call).
        Component.onCompleted: Qt.callLater(list._applyInitial)
        onContentWidthChanged: Qt.callLater(list._applyInitial)
        onContentXChanged: rail.horizontalPositionChanged(list.contentX)

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Left) { rail.navigate(-1); event.accepted = true; }
            else if (event.key === Qt.Key_Right) { rail.navigate(1); event.accepted = true; }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Select) {
                rail.activateFocused(); event.accepted = true;
            }
        }

        delegate: Item {
            id: cell
            required property var modelData
            required property int index

            width: rail.cellWidth
            height: list.height

            Text {
                id: rankNum
                visible: rail.ranked
                text: cell.index + 1
                color: Qt.rgba(1, 1, 1, 0.16)
                font.family: theme.display
                font.bold: true
                font.pixelSize: 132
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -8
            }

            CataloguePosterCard {
                // Automation identity (Lanista): theatre catalogue cards keep the
                // provider identity, so a production capture can open a real title
                // without a presentation-only card shell.
                objectName: (cell.modelData && (cell.modelData.id || cell.modelData.title))
                            ? "theatreCatalogCard_"
                              + String(cell.modelData.id || cell.modelData.title)
                                .replace(/[^A-Za-z0-9_]/g, "_")
                            : ""
                width: rail.posterWidth
                height: list.height
                visualProfile: rail.visualProfile
                // Theatre poster ratings all derive from IMDb; surface that on gallery hover only.
                hoverSourceText: rail._gallery ? "IMDb" : ""
                keyboardFocused: rail.railFocused && cell.index === rail.currentIndex
                anchors.left: rail.ranked ? rankNum.right : parent.left
                anchors.leftMargin: rail.ranked ? -32 : 0
                anchors.top: parent.top
                item: cell.modelData
                onActivated: (it) => rail.itemRequested(it)
            }
        }
    }
}
