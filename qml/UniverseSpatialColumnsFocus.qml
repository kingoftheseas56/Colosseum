import QtQuick

Item {
    id: root

    required property Flickable pageFlick
    required property Repeater columnRepeater
    property Flickable horizontalFlick: null
    property Item singletonItem: null
    property var columnsModel: []
    property string itemsProperty: "items"
    property bool appendSingleton: false
    property int currentColumn: firstSelectableColumn()
    property int currentItem: 0
    property string accessibleName: "Spatial media collection"
    signal activated(int column, int item, bool singleton)

    readonly property int columnCount: (root.columnsModel ? root.columnsModel.length : 0)
                                        + (root.appendSingleton ? 1 : 0)
    focusPolicy: root.visible && root.enabled && root.firstSelectableColumn() >= 0
                 ? Qt.TabFocus : Qt.NoFocus

    function itemCount(column) {
        const baseCount = root.columnsModel ? root.columnsModel.length : 0
        if (root.appendSingleton && column === baseCount)
            return 1
        if (column < 0 || column >= baseCount)
            return 0
        const model = root.columnsModel[column]
        const items = model ? model[root.itemsProperty] : null
        return items ? items.length : 0
    }
    function firstSelectableColumn() {
        for (let i = 0; i < root.columnCount; ++i) {
            if (root.itemCount(i) > 0)
                return i
        }
        return -1
    }
    function lastSelectableColumn() {
        for (let i = root.columnCount - 1; i >= 0; --i) {
            if (root.itemCount(i) > 0)
                return i
        }
        return -1
    }
    function adjacentColumn(from, delta) {
        for (let c = from + delta; c >= 0 && c < root.columnCount; c += delta) {
            if (root.itemCount(c) > 0)
                return c
        }
        return -1
    }
    function columnDelegate(column) {
        const baseCount = root.columnsModel ? root.columnsModel.length : 0
        if (root.appendSingleton && column === baseCount)
            return root.singletonItem
        return root.columnRepeater.itemAt(column)
    }
    function itemDelegate(column, item) {
        const col = root.columnDelegate(column)
        if (!col)
            return null
        const baseCount = root.columnsModel ? root.columnsModel.length : 0
        if (root.appendSingleton && column === baseCount)
            return col
        return col.itemRepeater ? col.itemRepeater.itemAt(item) : null
    }
    function reveal() {
        const col = root.columnDelegate(root.currentColumn)
        if (root.horizontalFlick && col) {
            const left = col.x
            const right = col.x + col.width
            const maxX = Math.max(0, root.horizontalFlick.contentWidth - root.horizontalFlick.width)
            if (left < root.horizontalFlick.contentX)
                root.horizontalFlick.contentX = Math.max(0, left)
            else if (right > root.horizontalFlick.contentX + root.horizontalFlick.width)
                root.horizontalFlick.contentX = Math.min(maxX, right - root.horizontalFlick.width)
        }
        const item = root.itemDelegate(root.currentColumn, root.currentItem)
        if (!item)
            return
        const p = item.mapToItem(root.pageFlick.contentItem, 0, 0)
        const top = p.y
        const bottom = p.y + item.height
        const maxY = Math.max(0, root.pageFlick.contentHeight - root.pageFlick.height)
        if (top < root.pageFlick.contentY)
            root.pageFlick.contentY = Math.max(0, top)
        else if (bottom > root.pageFlick.contentY + root.pageFlick.height)
            root.pageFlick.contentY = Math.min(maxY, bottom - root.pageFlick.height)
    }
    function moveColumn(delta) {
        const next = root.adjacentColumn(root.currentColumn, delta)
        if (next < 0)
            return false
        root.currentColumn = next
        root.currentItem = Math.min(root.currentItem, root.itemCount(next) - 1)
        root.reveal()
        return true
    }
    function moveItem(delta) {
        const next = root.currentItem + delta
        if (next < 0 || next >= root.itemCount(root.currentColumn))
            return false
        root.currentItem = next
        root.reveal()
        return true
    }

    onActiveFocusChanged: if (activeFocus) {
        if (root.currentColumn < 0 || root.itemCount(root.currentColumn) <= 0) {
            root.currentColumn = root.firstSelectableColumn()
            root.currentItem = 0
        }
        root.reveal()
    }

    Keys.onPressed: (event) => {
        let handled = false
        if (event.key === Qt.Key_Left)
            handled = root.moveColumn(-1)
        else if (event.key === Qt.Key_Right)
            handled = root.moveColumn(1)
        else if (event.key === Qt.Key_Up)
            handled = root.moveItem(-1)
        else if (event.key === Qt.Key_Down)
            handled = root.moveItem(1)
        else if (event.key === Qt.Key_Home) {
            const first = root.firstSelectableColumn()
            handled = first >= 0 && (root.currentColumn !== first || root.currentItem !== 0)
            if (handled) { root.currentColumn = first; root.currentItem = 0; root.reveal() }
        } else if (event.key === Qt.Key_End) {
            const last = root.lastSelectableColumn()
            const lastItem = last >= 0 ? root.itemCount(last) - 1 : -1
            handled = last >= 0 && (root.currentColumn !== last || root.currentItem !== lastItem)
            if (handled) { root.currentColumn = last; root.currentItem = lastItem; root.reveal() }
        } else if (event.key === Qt.Key_PageUp) {
            handled = root.currentItem !== 0
            if (handled) { root.currentItem = 0; root.reveal() }
        } else if (event.key === Qt.Key_PageDown) {
            const lastItem = root.itemCount(root.currentColumn) - 1
            handled = lastItem >= 0 && root.currentItem !== lastItem
            if (handled) { root.currentItem = lastItem; root.reveal() }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            const baseCount = root.columnsModel ? root.columnsModel.length : 0
            root.activated(root.currentColumn, root.currentItem,
                           root.appendSingleton && root.currentColumn === baseCount)
            handled = true
        }
        if (handled)
            event.accepted = true
    }

    Accessible.role: Accessible.List
    Accessible.name: root.accessibleName
    Accessible.focusable: focusPolicy !== Qt.NoFocus
}
