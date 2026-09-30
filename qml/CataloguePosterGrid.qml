// CataloguePosterGrid — the shared infinite poster wall extracted from DiscoverBrowser: the
// same column sizing, in-grid skeletons that reserve exact cells, keyboard navigation, and
// incremental-load trigger. It renders CataloguePosterCard delegates and leaves error/empty
// copy to the parent page. Used by the Theatre See-all page (and available to any catalogue).
import QtQuick
import QtQuick.Controls   // ScrollBar attached property
import "CatalogueVisualMetrics.js" as Metrics

GridView {
    id: wall

    property var items: []
    property bool loading: false          // first-page skeletons (nothing loaded yet)
    property bool loadingMore: false      // one trailing skeleton row while paging
    property bool hasMore: false
    property string emptyMessage: "Nothing here yet."
    property bool keyboardMode: false
    // "classic" (default; every un-cleared consumer stays pixel-identical) | "gallery" (approved polish).
    property string visualProfile: "classic"
    readonly property bool _gallery: wall.visualProfile === "gallery"
    readonly property var _gm: Metrics.gallery
    signal requestMore()
    signal itemRequested(var item)

    Theme { id: theme }

    clip: true
    interactive: true
    pixelAligned: false
    boundsBehavior: Flickable.StopAtBounds
    focus: true
    keyNavigationEnabled: true
    // Classic keeps its exact prior tuning (~132px tiles). Gallery derives its stride/height from the
    // gallery tokens: wider tiles (poster + card gap) and room for the two-line title reserve.
    readonly property int columnCount: Math.max(3, Math.floor(width / (wall._gallery
        ? (wall._gm.posterWidth + wall._gm.cardGap) : 146)))
    cellWidth: Math.floor(width / columnCount)
    cellHeight: wall._gallery
        // poster (card width × ratio) + title top gap + two-line reserve + the delegate's 14px inset + margin
        ? (Math.floor((cellWidth - 14) * wall._gm.posterRatio) + 10 + wall._gm.titleMinHeight + 14 + 6)
        : (Math.floor(cellWidth * 1.62) + 34)
    cacheBuffer: cellHeight * 2
    // in-grid skeletons reserve EXACT cell space: fill the viewport on the first page, one
    // trailing row while paging.
    readonly property int skelCount: (wall.loading && wall.items.length === 0)
        ? columnCount * Math.max(2, Math.ceil(height / cellHeight))
        : (wall.loadingMore ? columnCount : 0)
    model: wall.items.length + skelCount
    ScrollBar.vertical: HouseScrollBar { flick: wall }
    onContentYChanged: {
        if (wall.hasMore && !wall.loadingMore
            && contentHeight > height && contentY > contentHeight - height * 1.6)
            wall.requestMore()
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
            || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            wall.keyboardMode = true
            event.accepted = false            // let GridView move currentIndex
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Space || event.key === Qt.Key_Select) {
            wall.keyboardMode = true
            if (wall.currentIndex >= 0 && wall.currentIndex < wall.items.length)
                wall.itemRequested(wall.items[wall.currentIndex])
            event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            gridGlide.pageUp(); event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            gridGlide.pageDown(); event.accepted = true
        } else if (event.key === Qt.Key_Home) {
            gridGlide.toTop(); event.accepted = true
        } else if (event.key === Qt.Key_End) {
            gridGlide.toBottom(); event.accepted = true
        }
    }

    delegate: CataloguePosterCard {
        required property int index
        width: wall.cellWidth - 14
        height: wall.cellHeight - 14
        visualProfile: wall.visualProfile
        // gallery hover surfaces the rating's origin, matching the Theatre rails.
        hoverSourceText: wall._gallery ? "IMDb" : ""
        skeleton: index >= wall.items.length
        item: (index >= 0 && index < wall.items.length) ? wall.items[index] : null
        keyboardFocused: wall.keyboardMode && index === wall.currentIndex && index < wall.items.length
        onActivated: (it) => {
            wall.forceActiveFocus()
            wall.keyboardMode = false
            wall.currentIndex = index
            wall.itemRequested(it)
        }
    }

    // honest empty state (skeletons live in-grid, reserving exact cells)
    Text {
        visible: !wall.loading && wall.items.length === 0
        anchors.centerIn: parent
        text: wall.emptyMessage
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
    }

    // Shared wheel controller — unifies the See-all grid's scroll feel with every landing page
    // and Discover (fast accumulator drain, no double-scroll with GridView's default wheel).
    ScrollGlide { id: gridGlide; flick: wall }
}
