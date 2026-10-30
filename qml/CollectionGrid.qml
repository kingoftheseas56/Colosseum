import QtQuick
import "TouchMetrics.js" as TouchMetrics

GridView {
    id: grid
    property Item pageOwner: grid
    property bool pageActive: true
    property bool pageFlow: false
    property bool stretchPosters: false
    property real labelHeight: 36
    property bool allowWatched: false
    property var removeEntry: null
    readonly property alias selection: selectionState
    readonly property bool touch: layout.touch
    readonly property bool phoneLayout: layout.phoneLayout
    readonly property bool portraitPhoneLayout: layout.touch && layout.phone && layout.portrait
    readonly property int posterW: portraitPhoneLayout
        ? Math.min(94, Math.max(0, cellWidth - 12))
        : stretchPosters ? Math.max(0, cellWidth - 16)
        : phoneLayout ? Math.min(148, Math.max(0, cellWidth - 16)) : 148
    readonly property int posterH: Math.floor(posterW * 1.5)
    readonly property int columnCount: portraitPhoneLayout ? Math.max(1, Math.floor(width / 106))
        : phoneLayout ? Math.max(1, Math.floor(width / 128))
        : stretchPosters ? Math.max(2, Math.floor(width / 178))
        : Math.max(2, Math.floor((width + 20) / 168))
    readonly property real menuTargetSize: touch ? TouchMetrics.minimumTargetSize : 26
    signal removeRequested(var entry)
    signal watchedRequested(var entry, bool watched)

    AdaptiveLayout { id: layout; page: grid.pageOwner; viewportHeight: grid.height }
    LibrarySelectionState { id: selectionState; rows: grid.touch && grid.model ? grid.model : [] }
    LibrarySelectionHost {
        owner: grid.pageOwner
        selection: selectionState
        pageActive: grid.pageActive && grid.touch
        allowWatched: grid.allowWatched
        removeEntry: grid.removeEntry
        onRemoveRequested: (entry) => grid.removeRequested(entry)
        onWatchedRequested: (entry, watched) => grid.watchedRequested(entry, watched)
    }
    cellWidth: Math.floor(width / columnCount)
    cellHeight: posterH + 24 + Math.ceil(labelHeight)
    cacheBuffer: cellHeight * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    interactive: !pageFlow
    highlightFollowsCurrentItem: !pageFlow
}
