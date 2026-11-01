import QtQml

QtObject {
    id: root
    property var rows: []
    property var selectedKeys: []
    readonly property int count: selectedKeys.length
    readonly property bool active: count > 0
    readonly property var selectedRows: {
        const out = []
        for (const row of rows) if (contains(row)) out.push(row)
        return out
    }
    function key(row) { return row && row.entry ? String(row.entry.id || "") : "" }
    function contains(row) { return selectedKeys.indexOf(key(row)) >= 0 }
    function begin(row) {
        const id = key(row)
        if (id.length) selectedKeys = [id]
    }
    function toggle(row) {
        const id = key(row)
        if (!id.length) return
        const next = selectedKeys.slice()
        const index = next.indexOf(id)
        if (index >= 0) next.splice(index, 1)
        else next.push(id)
        selectedKeys = next
    }
    function selectAll() {
        const next = []
        for (const row of rows) {
            const id = key(row)
            if (id.length && next.indexOf(id) < 0) next.push(id)
        }
        selectedKeys = next
    }
    function invert() {
        const next = []
        for (const row of rows) {
            const id = key(row)
            if (id.length && !contains(row) && next.indexOf(id) < 0) next.push(id)
        }
        selectedKeys = next
    }
    function clear() { selectedKeys = [] }
    function prune() {
        const available = rows.map(row => key(row))
        selectedKeys = selectedKeys.filter(id => available.indexOf(id) >= 0)
    }
    onRowsChanged: prune()
}
