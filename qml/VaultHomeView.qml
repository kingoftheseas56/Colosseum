pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Flickable {
    id: home
    property var heroRows: []
    property var continueRows: []
    property var kindRows: ({})
    property Component footer: null
    signal activated(var row)
    signal resumeRequested(var row)
    signal detailsRequested(var row)
    signal hideRequested(var row)
    signal showKind(string kind)
    Theme { id: theme }
    contentWidth: width; contentHeight: body.height + 96
    clip: true; boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: HouseScrollBar { flick: home }
    function focusShelf(index) {
        let target = index < 0 ? continueShelf : (shelves.itemAt(index) as VaultLibraryShelf)
        if (index >= 0) {
            while (target && !target.visible && ++index < shelves.count) target = shelves.itemAt(index) as VaultLibraryShelf
        }
        if (target && target.visible) {             target.focusFirst();
             home.contentY = Math.max(0, Math.min(target.y - 30, home.contentHeight - home.height)) }
    }
    Column {
        id: body; width: home.width; spacing: 28
        VaultBillboard {
            objectName: "vaultHomeBillboard"
            width: parent.width; rows: home.heroRows; visible: rows.length > 0; height: visible ? 308 : 0
            onActivated: row => Number(row.progressFraction || 0) > 0 ? home.resumeRequested(row) : home.activated(row)
            onDetailsRequested: row => home.detailsRequested(row)
            onMoveDown: home.continueRows.length ? home.focusShelf(-1) : home.focusShelf(0)
        }
        VaultLibraryShelf {
            id: continueShelf; width: parent.width; title: "Continue"; rows: home.continueRows; continueShelf: true; visible: rows.length > 0
            onActivated: row => home.resumeRequested(row)
            onMoveDown: home.focusShelf(0)
        }
        Repeater {
            id: shelves
            model: [{kind: "movie", title: "Movies"}, {kind: "show", title: "Shows"}, {kind: "book", title: "Books"}, {kind: "comic", title: "Comics"}]
            delegate: VaultLibraryShelf {
                required property var modelData
                required property int index
                width: body.width; title: modelData.title; kind: modelData.kind
                rows: home.kindRows[kind] || []; visible: rows.length > 0
                onActivated: row => home.activated(row)
                onHideRequested: row => home.hideRequested(row)
                onShowKind: kind => home.showKind(kind)
                onMoveUp: { let n = index - 1; while (n >= 0 && !shelves.itemAt(n).visible) --n; home.focusShelf(n) }
                onMoveDown: { let n = index + 1; while (n < shelves.count && !shelves.itemAt(n).visible) ++n; home.focusShelf(n) }
            }
        }
        Loader { width: parent.width; sourceComponent: home.footer }
    }
}
