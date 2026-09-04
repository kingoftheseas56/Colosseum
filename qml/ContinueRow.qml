// ContinueRow — the resume row for a world page. A row IS the right shape for resume (per doctrine).
// One ContinueTile per entry (world variant): ALL metadata lives inside the tile (Hemanth
// 2026-07-05), the circle resumes, anywhere else opens detail, hover reveals ✕ remove.
// Watched entries sink below unfinished ones so the front of the row is always continuable.

import QtQuick
import QtQuick.Window
import "CatalogueVisualMetrics.js" as Metrics

Column {
    id: cont

    readonly property bool televisionMode: {
        const w = cont.Window.window
        return !!(w && w["televisionMode"] === true)
    }
    property int currentIndex: ordered.length > 0 ? 0 : -1

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

    onOrderedChanged: {
        if (ordered.length === 0) currentIndex = -1
        else currentIndex = Math.max(0, Math.min(currentIndex, ordered.length - 1))
    }
    function ensureCurrentVisible() {
        var item = tileRepeater.itemAt(cont.currentIndex)
        if (!item) return
        var left = item.x
        var right = item.x + item.width
        var maxX = Math.max(0, continueFlick.contentWidth - continueFlick.width)
        if (left < continueFlick.contentX)
            continueFlick.contentX = Math.max(0, left)
        else if (right > continueFlick.contentX + continueFlick.width)
            continueFlick.contentX = Math.min(maxX, right - continueFlick.width)
    }
    function navigate(delta) {
        if (ordered.length === 0) return false
        var next = Math.max(0, Math.min(ordered.length - 1, currentIndex + delta))
        if (next === currentIndex) return false
        currentIndex = next
        ensureCurrentVisible()
        return true
    }
    function activateCurrent() {
        if (currentIndex >= 0 && currentIndex < ordered.length)
            cont.resumeRequested(ordered[currentIndex])
    }

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
        property alias currentIndex: cont.currentIndex
        width: parent.width; height: cont.tileHeight
        contentWidth: row.width; contentHeight: height
        clip: true
        focusPolicy: cont.ordered.length > 0 ? Qt.TabFocus : Qt.NoFocus
        onCurrentIndexChanged: if (currentIndex >= cont.ordered.length) currentIndex = Math.max(-1, cont.ordered.length - 1)
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: cont.televisionMode && cont.visible && cont.ordered.length > 0
        Accessible.role: Accessible.List
        Accessible.name: cont.title
        Keys.onPressed: (event) => {
            // TV remote Select keeps the Android resume-first contract. Directional keys use
            // the same index/visibility law as Arc 41, and boundary arrows remain unaccepted.
            if (cont.televisionMode && event.key === Qt.Key_Left) {
                event.accepted = cont.navigate(-1)
                return
            }
            if (cont.televisionMode && event.key === Qt.Key_Right) {
                event.accepted = cont.navigate(1)
                return
            }
            if (cont.televisionMode && event.key === Qt.Key_Select) {
                cont.activateCurrent()
                event.accepted = true
                return
            }
            continueKeys.handle(event)
        }

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
                    focusManagedByCollection: true
                    keyboardFocused: continueFlick.activeFocus && continueFlick.currentIndex === index
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
