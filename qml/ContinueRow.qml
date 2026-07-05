// ContinueRow — the resume row for a world page. A row IS the right shape for resume (per doctrine).
// One ContinueTile per entry (world variant): ALL metadata lives inside the tile (Hemanth
// 2026-07-05), the circle resumes, anywhere else opens detail, hover reveals ✕ remove.
// Watched entries sink below unfinished ones so the front of the row is always continuable.

import QtQuick

Column {
    id: cont

    property string title: "Continue"
    property var items: []              // Progress entries: { id, kind, title|caption, sub, cover, c1, c2, progress, watched, resume }
    signal resumeRequested(var item)    // center icon → resume INTO the content
    signal detailRequested(var item)    // anywhere else → the series / detail view

    // unfinished first (both halves keep their recency order), watched sink to the back
    readonly property var ordered: items.filter(function(e) { return e.watched !== true })
                                        .concat(items.filter(function(e) { return e.watched === true }))

    width: parent ? parent.width : 800
    spacing: 14
    // The resume shelf only exists when there's something on it. Its parent board is a Column, which
    // skips invisible children — so an empty Continue leaves no gap.
    visible: cont.items.length > 0

    // navigable off: the '›' affordance led nowhere (audit) — it returns when a see-all page exists
    WidgetHeader { width: parent.width; title: cont.title; navigable: false }

    Flickable {
        width: parent.width; height: 196
        contentWidth: row.width; contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            spacing: 18
            Repeater {
                model: cont.ordered
                delegate: ContinueTile {
                    required property var modelData
                    variant: "world"
                    entry: modelData
                    onResumeRequested: cont.resumeRequested(modelData)
                    onDetailRequested: cont.detailRequested(modelData)
                    onRemoveRequested: Progress.forget(modelData.kind, modelData.id)
                }
            }
        }
    }
}
