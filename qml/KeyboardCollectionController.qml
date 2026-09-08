// KeyboardCollectionController — shared spatial keyboard law for Colosseum media collections.
// The owning view keeps focus; this controller moves currentIndex, keeps it visible, and emits
// semantic activation/context/reorder requests. Boundary arrows remain unaccepted so a parent
// FocusScope can move focus to the neighbouring region.
import QtQuick

Item {
    id: nav

    required property var view
    property string orientation: "vertical" // vertical | horizontal | grid
    property int columns: 1
    property int count: view && view.count !== undefined ? view.count : 0
    property int currentIndex: view && view.currentIndex !== undefined ? view.currentIndex : -1
    property int pageStep: 0
    // Optional seam for Flickable/Row collections without positionViewAtIndex().
    property var positionIndexFn: null
    // Enter is the collection's universal primary action. Space is explicit opt-in
    // (player/toggle/native semantics), not an automatic second Enter.
    property bool spaceActivates: false
    property bool contextEnabled: false
    property bool reorderEnabled: false
    property bool keyboardRecentlyMoved: false
    property int keyboardQuietMs: 220
    property int positionMode: GridView.Contain
    // Optional collection geometry/identity seams. Native views may supply these
    // without requiring delegate realization or a scan of instantiated children.
    property var rowLengths: null
    property var rowForIndex: null
    property var identityForIndex: null
    property var indexForIdentity: null
    property int modelRevision: 0
    property var _laneReturnId: null
    property int _laneReturnColumn: -1
    property int _laneReturnRow: -1
    property int _laneRevision: -1
    property int _laneColumn: -1
    property bool _lanePending: false

    signal activated(int index)
    signal contextRequested(int index)
    signal reorderRequested(int fromIndex, int toIndex)

    // A revision/count change can be an incremental reorder or removal. Keep
    // the pending semantic lane alive so index resolution can recover the
    // surviving identity; route owners can call invalidateLane() explicitly
    // when replacing the surface.
    onModelRevisionChanged: {
        if (!nav._lanePending)
            nav.invalidateLane()
    }
    onViewChanged: nav.invalidateLane()
    onCountChanged: {
        if (!nav._lanePending)
            nav.invalidateLane()
    }

    visible: false

    Timer {
        id: keyboardQuietTimer
        interval: nav.keyboardQuietMs
        onTriggered: nav.keyboardRecentlyMoved = false
    }

    function markKeyboardMovement() {
        nav.keyboardRecentlyMoved = true
        keyboardQuietTimer.restart()
    }

    function indexNow() {
        if (nav.view && nav.view.currentIndex !== undefined)
            return nav.view.currentIndex
        return nav.currentIndex
    }

    function invalidateLane() {
        nav._laneReturnId = null
        nav._laneReturnColumn = -1
        nav._laneReturnRow = -1
        nav._laneColumn = -1
        nav._lanePending = false
        nav._laneRevision = nav.modelRevision
    }

    function _syncLaneRevision() {
        if (nav._laneRevision === nav.modelRevision)
            return
        // A live pending lane is an incremental mutation contract: resolve its
        // surviving identity against the new model before falling back to its
        // remembered column. Replacement callers explicitly invalidateLane().
        if (nav._lanePending)
            nav._laneRevision = nav.modelRevision
        else
            nav.invalidateLane()
    }

    function _rowFor(index) {
        if (nav.rowForIndex)
            return Math.max(0, Number(nav.rowForIndex(index)))
        if (nav.rowLengths && nav.rowLengths.length) {
            var start = 0
            for (var row = 0; row < nav.rowLengths.length; ++row) {
                var length = Math.max(0, Number(nav.rowLengths[row]))
                if (index < start + length)
                    return row
                start += length
            }
            return Math.max(0, nav.rowLengths.length - 1)
        }
        return Math.floor(Math.max(0, index) / Math.max(1, nav.columns))
    }

    function _rowLength(row) {
        if (row < 0)
            return 0
        if (nav.rowLengths && nav.rowLengths.length)
            return Math.max(0, Number(nav.rowLengths[row] || 0))
        var start = row * Math.max(1, nav.columns)
        return Math.max(0, Math.min(Math.max(1, nav.columns), nav.count - start))
    }

    function _columnFor(index) {
        if (nav.rowLengths && nav.rowLengths.length) {
            var start = 0
            for (var row = 0; row < nav.rowLengths.length; ++row) {
                var length = Math.max(0, Number(nav.rowLengths[row]))
                if (index < start + length)
                    return index - start
                start += length
            }
        }
        return Math.max(0, index) % Math.max(1, nav.columns)
    }

    function _indexForRowColumn(row, column) {
        if (row < 0 || row >= (nav.rowLengths && nav.rowLengths.length
                ? nav.rowLengths.length : Math.ceil(nav.count / Math.max(1, nav.columns))))
            return -1
        var length = nav._rowLength(row)
        if (length <= 0)
            return -1
        var targetColumn = Math.max(0, Math.min(length - 1, column))
        if (nav.rowLengths && nav.rowLengths.length) {
            var start = 0
            for (var i = 0; i < row; ++i)
                start += Math.max(0, Number(nav.rowLengths[i]))
            return start + targetColumn
        }
        return row * Math.max(1, nav.columns) + targetColumn
    }

    function _identityAt(index) {
        if (nav.identityForIndex)
            return nav.identityForIndex(index)
        if (nav.view && nav.view.identityForIndex)
            return nav.view.identityForIndex(index)
        return null
    }

    function _indexForIdentity(identity) {
        if (identity === null || identity === undefined || identity === "")
            return -1
        var result = nav.indexForIdentity
            ? nav.indexForIdentity(identity)
            : (nav.view && nav.view.indexForIdentity
                ? nav.view.indexForIdentity(identity) : -1)
        return isFinite(Number(result)) ? Number(result) : -1
    }
    function moveTo(index, focusReason) {
        const n = Math.max(0, nav.count)
        if (!nav.view || n <= 0)
            return false
        const next = Math.max(0, Math.min(n - 1, index))
        if (next === nav.indexNow())
            return false
        nav.view.currentIndex = next
        nav.currentIndex = next
        if (nav.positionIndexFn)
            nav.positionIndexFn(next)
        else if (nav.view.positionViewAtIndex)
            nav.view.positionViewAtIndex(next, nav.positionMode)
        if (nav.view.forceActiveFocus)
            nav.view.forceActiveFocus(focusReason)
        nav.markKeyboardMovement()
        return true
    }

    function defaultPageStep() {
        if (!nav.view)
            return 1
        if (nav.orientation === "grid") {
            const rowHeight = Number(nav.view.cellHeight) || 1
            const visibleRows = Math.max(1, Math.floor(Number(nav.view.height) / rowHeight))
            return visibleRows * Math.max(1, nav.columns)
        }
        if (nav.orientation === "horizontal") {
            const itemWidth = nav.view.currentItem ? Number(nav.view.currentItem.width) : 0
            return Math.max(1, itemWidth > 0 ? Math.floor(Number(nav.view.width) / itemWidth) : 5)
        }
        const itemHeight = nav.view.currentItem ? Number(nav.view.currentItem.height) : 0
        return Math.max(1, itemHeight > 0 ? Math.floor(Number(nav.view.height) / itemHeight) : 8)
    }

    function stepForKey(key) {
        const cols = Math.max(1, nav.columns)
        if (nav.orientation === "horizontal") {
            if (key === Qt.Key_Left) return -1
            if (key === Qt.Key_Right) return 1
            return 0
        }
        if (nav.orientation === "grid") {
            if (key === Qt.Key_Left) return -1
            if (key === Qt.Key_Right) return 1
            if (key === Qt.Key_Up) return -cols
            if (key === Qt.Key_Down) return cols
            return 0
        }
        if (key === Qt.Key_Up) return -1
        if (key === Qt.Key_Down) return 1
        return 0
    }
    function stepAllowed(index, step) {
        if (step === 0)
            return false
        const next = index + step
        if (next < 0 || next >= nav.count)
            return false
        if (nav.orientation === "grid" && Math.abs(step) === 1)
            return nav._rowFor(index) === nav._rowFor(next)
        return true
    }

    function directionalTarget(index, key) {
        nav._syncLaneRevision()
        if (nav.orientation !== "grid") {
            const step = nav.stepForKey(key)
            return nav.stepAllowed(index, step) ? index + step : -1
        }

        const row = nav._rowFor(index)
        const column = nav._columnFor(index)
        const rowLength = nav._rowLength(row)

        if (key === Qt.Key_Left)
            return column > 0 ? index - 1 : -1
        if (key === Qt.Key_Right)
            return column + 1 < rowLength ? index + 1 : -1

        const targetRow = key === Qt.Key_Up ? row - 1
            : (key === Qt.Key_Down ? row + 1 : -1)
        if (targetRow < 0)
            return -1
        if (targetRow < 0 || nav._rowLength(targetRow) <= 0)
            return -1
        var intendedColumn = nav._lanePending && nav._laneColumn >= 0
            ? nav._laneColumn : column
        if (key === Qt.Key_Up && nav._lanePending) {
            var returned = nav._indexForIdentity(nav._laneReturnId)
            if (returned >= 0 && nav._rowFor(returned) === targetRow)
                return returned
            if (nav._laneReturnColumn >= 0)
                intendedColumn = nav._laneReturnColumn
            else if (nav._laneColumn >= 0)
                intendedColumn = nav._laneColumn
        }
        return nav._indexForRowColumn(targetRow, intendedColumn)
    }

    function reorderDeltaFor(event) {
        if (!nav.reorderEnabled
                || !(event.modifiers & Qt.ControlModifier)
                || !(event.modifiers & Qt.ShiftModifier))
            return 0
        return nav.stepForKey(event.key)
    }

    function contextKey(event) {
        return event.key === Qt.Key_Menu
            || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))
    }

    function activateKey(event) {
        return event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            || (nav.spaceActivates && event.key === Qt.Key_Space)
    }
    function handle(event) {
        if (!event || nav.count <= 0)
            return false
        const index = Math.max(0, nav.indexNow())
        const reorderDelta = nav.reorderDeltaFor(event)
        if (reorderDelta !== 0 && nav.stepAllowed(index, reorderDelta)) {
            nav.reorderRequested(index, index + reorderDelta)
            event.accepted = true
            nav.markKeyboardMovement()
            return true
        }
        if (nav.activateKey(event)) {
            nav.activated(index)
            event.accepted = true
            return true
        }
        if (nav.contextEnabled && nav.contextKey(event)) {
            nav.contextRequested(index)
            event.accepted = true
            return true
        }

        const directional = nav.directionalTarget(index, event.key)
        if (directional >= 0 && directional !== index) {
            const vertical = nav.orientation === "grid"
                && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)
            if (vertical && event.key === Qt.Key_Down && !nav._lanePending) {
                nav._laneReturnId = nav._identityAt(index)
                nav._laneReturnColumn = nav._columnFor(index)
                nav._laneReturnRow = nav._rowFor(index)
                nav._laneColumn = nav._columnFor(index)
                nav._lanePending = true
                nav._laneRevision = nav.modelRevision
            } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                nav.invalidateLane()
            }
            const backward = event.key === Qt.Key_Up || event.key === Qt.Key_Left
            const reason = backward ? Qt.BacktabFocusReason : Qt.TabFocusReason
            if (nav.moveTo(directional, reason)) {
                if (vertical && event.key === Qt.Key_Up) {
                    nav._lanePending = nav._rowFor(directional) > 0
                }
                event.accepted = true
                return true
            }
        }
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
            nav.invalidateLane()
        if (event.key === Qt.Key_Home) {
            nav.invalidateLane()
            if (nav.moveTo(0, Qt.BacktabFocusReason)) {
                event.accepted = true
                return true
            }
        } else if (event.key === Qt.Key_End) {
            nav.invalidateLane()
            if (nav.moveTo(nav.count - 1, Qt.TabFocusReason)) {
                event.accepted = true
                return true
            }
        } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
            nav.invalidateLane()
            const page = nav.pageStep > 0 ? nav.pageStep : nav.defaultPageStep()
            const delta = event.key === Qt.Key_PageUp ? -page : page
            const reason = delta < 0 ? Qt.BacktabFocusReason : Qt.TabFocusReason
            if (nav.moveTo(index + delta, reason)) {
                event.accepted = true
                return true
            }
        }
        return false
    }
}
