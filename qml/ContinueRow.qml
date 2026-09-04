// ContinueRow — the resume row for a world page. A row IS the right shape for resume (per doctrine).
// One ContinueTile per entry (world variant): ALL metadata lives inside the tile (Hemanth
// 2026-07-05), the circle resumes, anywhere else opens detail, hover reveals ✕ remove.
// Watched entries sink below unfinished ones so the front of the row is always continuable.

import QtQuick
import "CatalogueVisualMetrics.js" as Metrics

Column {
    id: cont

    // the resume tiles now share the catalogue gallery poster geometry (see ContinueTile world variant)
    readonly property int tileHeight: Math.round(Metrics.gallery.posterWidth * Metrics.gallery.posterRatio)

    property string title: "Continue"
    property var items: []              // Progress entries: { id, kind, title|caption, sub, cover, c1, c2, progress, watched, resume }
    signal resumeRequested(var item)    // center icon → resume INTO the content
    signal detailRequested(var item)    // anywhere else → the series / detail view
    signal seeAllRequested()            // header "See all ›" → the scoped see-all page

    // Optional: rows not backed by Progress (e.g. Your Collection) supply their own
    // remove. Default preserves the Progress.forget wiring untouched.
    property var forgetHandler: null

    // Rows without a see-all destination (Your Collection) hide the header chevron.
    property bool showSeeAll: true

    // unfinished first (both halves keep their recency order), watched sink to the back
    readonly property var ordered: items.filter(function(e) { return e.watched !== true })
                                        .concat(items.filter(function(e) { return e.watched === true }))

    width: parent ? parent.width : 800
    spacing: 14
    // The resume shelf only exists when there's something on it. Its parent board is a Column, which
    // skips invisible children — so an empty Continue leaves no gap.
    visible: cont.items.length > 0

    // the '›' is honest now — it opens the scoped see-all page (audit debt paid 2026-07-11)
    WidgetHeader {
        width: parent.width; title: cont.title
        moreLabel: "See all"
        navigable: cont.showSeeAll
        onMoreClicked: cont.seeAllRequested()
    }

    Flickable {
        id: continueFlick
        property int currentIndex: cont.ordered.length > 0 ? 0 : -1
        width: parent.width; height: cont.tileHeight
        contentWidth: row.width; contentHeight: height
        clip: true
        focusPolicy: cont.ordered.length > 0 ? Qt.TabFocus : Qt.NoFocus
        onCurrentIndexChanged: if (currentIndex >= cont.ordered.length) currentIndex = Math.max(-1, cont.ordered.length - 1)
        Keys.onPressed: (event) => continueKeys.handle(event)
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            spacing: Metrics.gallery.cardGap
            Repeater {
                id: tileRepeater
                model: cont.ordered
                delegate: ContinueTile {
                    required property var modelData
                    required property int index
                    variant: "world"
                    collectionManaged: true
                    collectionSelected: continueFlick.activeFocus && continueFlick.currentIndex === index
                    entry: modelData
                    onResumeRequested: cont.resumeRequested(modelData)
                    onDetailRequested: cont.detailRequested(modelData)
                    onRemoveRequested: cont.forgetHandler
                        ? cont.forgetHandler(modelData)
                        : Progress.forget(modelData.kind, modelData.id)
                }
            }
        }

        KeyboardCollectionController {
            id: continueKeys
            view: continueFlick
            orientation: "horizontal"
            count: cont.ordered.length
            contextEnabled: true
            positionIndexFn: function(index) {
                const tileItem = tileRepeater.itemAt(index)
                if (!tileItem) return
                const left = tileItem.x
                const right = tileItem.x + tileItem.width
                if (left < continueFlick.contentX) continueFlick.contentX = left
                else if (right > continueFlick.contentX + continueFlick.width)
                    continueFlick.contentX = Math.min(Math.max(0, continueFlick.contentWidth - continueFlick.width), right - continueFlick.width)
            }
            onActivated: (index) => {
                const tileItem = tileRepeater.itemAt(index)
                if (tileItem) tileItem.detailRequested()
            }
            onContextRequested: (index) => {
                const tileItem = tileRepeater.itemAt(index)
                if (tileItem) tileItem.openKeyboardContext(continueFlick)
            }
        }
    }
}
